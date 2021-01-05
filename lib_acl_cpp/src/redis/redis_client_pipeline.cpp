#include "acl_stdafx.hpp"
#ifndef ACL_PREPARE_COMPILE
#include "acl_cpp/stdlib/token_tree.hpp"
#include "acl_cpp/redis/redis_client.hpp"
#include "acl_cpp/redis/redis_command.hpp"
#include "acl_cpp/redis/redis_cluster.hpp"
#include "acl_cpp/redis/redis_client_pipeline.hpp"
#endif

#if !defined(ACL_CLIENT_ONLY) && !defined(ACL_REDIS_DISABLE)

namespace acl {

redis_pipeline_channel::redis_pipeline_channel(redis_client_pipeline& pipeline,
	const char* addr, int conn_timeout, int rw_timeout, bool retry)
: pipeline_(pipeline)
, addr_(addr)
, buf_(81920)
{
	conn_ = NEW redis_client(addr, conn_timeout, rw_timeout, retry);
}

redis_pipeline_channel::~redis_pipeline_channel(void)
{
	delete conn_;
}

redis_pipeline_channel & redis_pipeline_channel::set_passwd(const char *passwd) {
	if (passwd && *passwd) {
		passwd_ = passwd;
	}
	return *this;
}

bool redis_pipeline_channel::start_thread(void)
{
	if (!passwd_.empty()) {
		conn_->set_password(passwd_);
	}
	if (!((connect_client*) conn_)->open()) {
		logger_error("open %s error %s", addr_.c_str(), last_serror());
		return false;
	}
	this->start();
	return true;
}

void redis_pipeline_channel::push(redis_pipeline_message* msg)
{
	msgs_.push_back(msg);
#ifdef USE_MBOX
	box_.push(msg);
#else
	box_.push(msg, false);
#endif
}

void redis_pipeline_channel::flush(void)
{
	if (msgs_.empty()) {
		return;
	}

	buf_.clear();

	for (std::vector<redis_pipeline_message*>::iterator it = msgs_.begin();
		it != msgs_.end(); ++it) {
#if 0
		string* req = (*it)->get_cmd().get_request_buf();
		buf_.append(req->c_str(), req->size());
#else
		redis_command::build_request((*it)->argc_, (*it)->argv_,
			(*it)->lens_, buf_);
#endif
	}
	msgs_.clear();

#ifdef DEBUG_BOX
	printf(">>>%s<<<\r\n", buf_.c_str());
#endif

	socket_stream* conn = conn_->get_stream();
	if (conn == NULL) {
		printf("conn NULL\r\n");
		exit(1);
	}

	if (conn->write(buf_) == -1) {
		printf("write error, addr=%s, buf=%s\r\n",
			addr_.c_str(), buf_.c_str());
		exit(1);
	}
#ifdef DEBUG_BOX
	printf("write ok, nmsg=%ld\n", msgs.size());
#endif
}

void* redis_pipeline_channel::run(void)
{
	dbuf_pool* dbuf;
	const redis_result* result;
	size_t nchild;
	int* timeout;

	while (!conn_->eof()) {
		redis_pipeline_message* msg = box_.pop();
		if (msg == NULL) {
			break;
		}

#ifdef DEBUG_BOX
		printf("reader: get msg\r\n");
#endif
		socket_stream* conn = conn_->get_stream();
		if (conn == NULL) {
			printf("get_stream null\r\n");
			break;
		}

		dbuf = msg->get_cmd().get_dbuf();
		timeout = msg->get_timeout();
		if (timeout) {
			conn->set_rw_timeout(*timeout);
		}

		nchild = msg->get_nchild();
		if (nchild >= 1) {
			result = conn_->get_objects(*conn, dbuf, nchild);
		} else {
			result = conn_->get_object(*conn, dbuf);
		}
		int type = result->get_type();
		if (type == REDIS_RESULT_UNKOWN || type != REDIS_RESULT_ERROR) {
			msg->push(result);
			continue;
		}

#define	EQ(x, y) !strncasecmp((x), (y), sizeof(y) -1)

		const char* ptr = result->get_error();
		if (ptr == NULL || *ptr == 0) {
			msg->push(result);
		} else if (EQ(ptr, "MOVED")) {
			const char* addr = msg->get_cmd().get_addr(ptr);
			if (addr == NULL || msg->get_redirect_count() >= 5) {
				msg->push(result);
			} else {
				msg->set_redirect_addr(addr);
				pipeline_.push(msg);
			}
		} else if (EQ(ptr, "ASK")) {

		} else if (EQ(ptr, "CLUSTERDOWN")) {

		} else {
			msg->push(result);
		}
	}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////////

redis_client_pipeline::redis_client_pipeline(const char* addr)
: addr_(addr)
, max_slot_(16384)
, conn_timeout_(10)
, rw_timeout_(10)
, retry_(true)
, nchannels_(1)
{
	slot_addrs_ = (const char**) acl_mycalloc(max_slot_, sizeof(char*));
	channels_   = NEW token_tree;
}

redis_client_pipeline::~redis_client_pipeline(void)
{
	for (std::vector<char*>::iterator it = addrs_.begin();
		it != addrs_.end(); ++it) {
		acl_myfree(*it);
	}
	acl_myfree(slot_addrs_);
	delete channels_;
}

redis_client_pipeline& redis_client_pipeline::set_timeout(int conn_timeout,
	int rw_timeout)
{
	conn_timeout_ = conn_timeout;
	rw_timeout_   = rw_timeout;
	return *this;
}

redis_client_pipeline& redis_client_pipeline::set_retry(bool on)
{
	retry_ = on;
	return *this;
}

redis_client_pipeline& redis_client_pipeline::set_channels(size_t n)
{
	nchannels_ = n;
	return *this;
}

redis_client_pipeline& redis_client_pipeline::set_password(const char* passwd)
{
	if (passwd && *passwd) {
		passwd_ = passwd;
	}
	return *this;
}

redis_client_pipeline & redis_client_pipeline::set_max_slot(size_t max_slot) {
	max_slot_ = max_slot;
	return *this;
}

const redis_result* redis_client_pipeline::run(redis_pipeline_message& msg)
{
#ifdef USE_MBOX
	box_.push(&msg);
#else
	box_.push(&msg, false);
#endif

	return msg.wait();
}

void redis_client_pipeline::push(redis_pipeline_message *msg)
{
#ifdef USE_MBOX
	box_.push(msg);
#else
	box_.push(msg, false);
#endif
}

void* redis_client_pipeline::run(void)
{
	set_all_slot();
	start_channels();

	if (channels_->first_node() == NULL) {
		logger_error("no channel created!");
		return NULL;
	}

	redis_pipeline_channel* channel;
	int  timeout = -1;
#ifdef USE_MBOX
	bool success;
#else
	bool found;
#endif

	while (true) {
#ifdef USE_MBOX
		redis_pipeline_message* msg = box_.pop(timeout, &success);
#else
		redis_pipeline_message* msg = box_.pop(timeout, &found);
#endif

#ifdef DEBUG_BOX
		printf("peek one msg=%p, timeout=%d\r\n", msg, timeout);
#endif
		if (msg != NULL) {
			int slot = msg->get_cmd().get_slot();
			const char* redirect_addr = msg->get_redirect_addr();
			if (redirect_addr) {
				set_slot(slot, redirect_addr);
			}
			channel = get_channel(slot);
			if (channel == NULL) {
				printf("channel null, slot=%d\r\n", slot);
				exit(1);
			}
			channel->push(msg);
			timeout = 0;
#ifdef USE_MBOX
		} else if (!success) {
#else
		} else if (found) {
#endif
			break;
		} else {
			timeout = -1;
			flush_all();
		}
	}

	printf("Exiting ...\r\n");
	return NULL;
}

void redis_client_pipeline::flush_all(void)
{
	const token_node* iter = channels_->first_node();
	while (iter) {
		redis_pipeline_channel* channel = (redis_pipeline_channel*)
			iter->get_ctx();
		channel->flush();
		iter = channels_->next_node();
	}
}

void redis_client_pipeline::set_slot(size_t slot, const char* addr)
{
	if (slot >= max_slot_ || addr == NULL || *addr == 0) {
		return;
	}

	// ������������е�ַ�����õ�ַ��������ֱ����ӣ�Ȼ��ʹ֮�� slot ���й���

	std::vector<char*>::const_iterator cit = addrs_.begin();
	for (; cit != addrs_.end(); ++cit) {
		if (strcmp((*cit), addr) == 0) {
			break;
		}
	}

	// �� slot ���ַ���й���ӳ��
	if (cit != addrs_.end()) {
		slot_addrs_[slot] = *cit;
	} else {
		// ֻ���Բ��ö�̬���䷽ʽ������Ϊ������������Ӷ���ʱ������
		// �����������̬����������ӵĶ�̬�ڴ��ַ���ǹ̶��ģ�����
		// slot_addrs_ ���±��ַҲ����Բ����
		char* buf = acl_mystrdup(addr);
		addrs_.push_back(buf);
		slot_addrs_[slot] = buf;
	}
}

void redis_client_pipeline::set_all_slot(void)
{
	redis_client client(addr_, 30, 60, false);

	if (!passwd_.empty()) {
		client.set_password(passwd_);
	}

	redis_cluster cluster(&client);

	const std::vector<redis_slot*>* slots = cluster.cluster_slots();
	if (slots == NULL) {
		logger("can't get cluster slots");
		return;
	}

	std::vector<redis_slot*>::const_iterator cit;
	for (cit = slots->begin(); cit != slots->end(); ++cit) {
		const redis_slot* slot = *cit;
		const char* ip = slot->get_ip();
		int port = slot->get_port();
		if (*ip == 0 || port <= 0 || port > 65535) {
			continue;
		}

		size_t slot_min = slot->get_slot_min();
		size_t slot_max = slot->get_slot_max();
		if (slot_max >= max_slot_ || slot_max < slot_min) {
			continue;
		}

		char buf[128];
		safe_snprintf(buf, sizeof(buf), "%s:%d", ip, port);
		for (size_t i = slot_min; i <= slot_max; i++) {
			set_slot(i, buf);
		}
	}
}

void redis_client_pipeline::start_channels(void) {
	for (std::vector<char*>::const_iterator cit = addrs_.begin();
		cit != addrs_.end(); ++cit) {
		(void) start_channel(*cit);
	}

	// ����Ѿ��ɹ�����˼�Ⱥ�ڵ㣬��˵��Ϊ��Ⱥģʽ������Ⱥ��ʽ�Դ���
	// ���򣬰����㷽ʽ�Դ�
	if (channels_->first_node() == NULL) {
		(void) start_channel(addr_);
	}
}

redis_pipeline_channel* redis_client_pipeline::start_channel(const char *addr)
{
	redis_pipeline_channel* channel = NEW redis_pipeline_channel(
		*this, addr, conn_timeout_, rw_timeout_, retry_);
	if (!passwd_.empty()) {
		channel->set_passwd(passwd_);
	}
	if (channel->start_thread()) {
		channels_->insert(addr, channel);
		return channel;
	} else {
		delete channel;
		return NULL;
	}
}

redis_pipeline_channel* redis_client_pipeline::get_channel(int slot) {
	const char* addr;
	if (slot >= 0 && slot < (int) max_slot_) {
		addr = slot_addrs_[slot];
		if (addr == NULL) {
			addr = addr_.c_str();
		}
	} else {
		addr = addr_.c_str();
	}

	const token_node* node = channels_->find(addr);
	if (node) {
		return (redis_pipeline_channel*) node->get_ctx();
	}

	return start_channel(addr);
}

} // namespace acl

#endif // ACL_CLIENT_ONLY
