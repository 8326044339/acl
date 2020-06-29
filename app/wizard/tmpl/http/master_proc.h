#pragma once

class http_service : public acl::master_proc
{
public:
	http_service(void);
	~http_service(void);

public:
	// Register all Http handlers with the http url path

	http_service& Get(const char* path, http_handler_t fn);
	http_service& Post(const char* path, http_handler_t fn);
	http_service& Head(const char* path, http_handler_t fn);
	http_service& Put(const char* path, http_handler_t fn);
	http_service& Patch(const char* path, http_handler_t fn);
	http_service& Connect(const char* path, http_handler_t fn);
	http_service& Purge(const char* path, http_handler_t fn);
	http_service& Delete(const char* path, http_handler_t fn);
	http_service& Options(const char* path, http_handler_t fn);
	http_service& Propfind(const char* path, http_handler_t fn);
	http_service& Websocket(const char* path, http_handler_t fn);
	http_service& Unknown(const char* path, http_handler_t fn);
	http_service& Error(const char* path, http_handler_t fn);

public:
	/**
	 * ���� CGI ��ʽ����ʱ�����
	 */
	void do_cgi(void);

protected:
	/**
	 * @override
	 * �麯���������յ�һ���ͻ�������ʱ���ô˺���
	 * @param stream {aio_socket_stream*} �½��յ��Ŀͻ����첽������
	 * ע���ú������غ������ӽ��ᱻ�رգ��û���Ӧ�����رո���
	 */
	void on_accept(acl::socket_stream* stream);

	/**
	 * @override
	 * �ڽ�������ʱ���������ÿ�ɹ�����һ�����ص�ַ������ñ�����
	 * @param ss {acl::server_socket&} ��������
	 */
	void proc_on_listen(acl::server_socket& ss);

	/**
	 * @override
	 * �������л��û���ݺ���õĻص��������˺���������ʱ������
	 * ��Ȩ��Ϊ��ͨ���޼���
	 */
	void proc_on_init(void);

	/**
	 * @override
	 * �������˳�ǰ���õĻص�����
	 */
	void proc_on_exit(void);

	/**
	 * @override
	 * �������յ� SIGHUP �źź�Ļص�����
	 */
	bool proc_on_sighup(acl::string&);

private:
	http_handlers_t handlers_[http_handler_max];

	void Service(int type, const char* path, http_handler_t fn);
};
