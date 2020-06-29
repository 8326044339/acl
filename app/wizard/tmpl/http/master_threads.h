#pragma once

class http_service : public acl::master_threads
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

protected:
	/**
	 * @override
	 * �麯������ĳ���ͻ������������ݿɶ���رջ����ʱ���ô˺���
	 * @param stream {socket_stream*}
	 * @return {bool} ���� false ���ʾ���������غ���Ҫ�ر����ӣ�
	 *  �����ʾ��Ҫ���ֳ����ӣ��������������Ӧ��Ӧ�÷��� false
	 */
	bool thread_on_read(acl::socket_stream* stream);

	/**
	 * @override
	 * ���̳߳��е�ĳ���̻߳��һ������ʱ�Ļص�������
	 * ���������һЩ��ʼ������
	 * @param stream {socket_stream*}
	 * @return {bool} ������� false ���ʾ����Ҫ��ر����ӣ�����
	 *  �ؽ��������ٴ����� thread_main ����
	 */
	bool thread_on_accept(acl::socket_stream* stream);

	/**
	 * @override
	 * ��ĳ���������ӵ� IO ��д��ʱʱ�Ļص�����������ú������� true ��
	 * ��ʾ�����ȴ���һ�ζ�д��������ϣ���رո�����
	 * @param stream {socket_stream*}
	 * @return {bool} ������� false ���ʾ����Ҫ��ر����ӣ�����
	 *  �ؽ��������ٴ����� thread_main ����
	 */
	bool thread_on_timeout(acl::socket_stream* stream);

	/**
	 * @override
	 * ����ĳ���̰߳󶨵����ӹر�ʱ�Ļص�����
	 * @param stream {socket_stream*}
	 */
	void thread_on_close(acl::socket_stream* stream);

	/**
	 * @override
	 * ���̳߳���һ�����̱߳�����ʱ�Ļص�����
	 */
	void thread_on_init(void);

	/**
	 * @override
	 * ���̳߳���һ���߳��˳�ʱ�Ļص�����
	 */
	void thread_on_exit(void);

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
	 * ���ӽ�����Ҫ�˳�ʱ��ܽ��ص��˺�������ܾ����ӽ����Ƿ��˳�ȡ���ڣ�
	 * 1) ����˺������� true ���ӽ��������˳�������
	 * 2) ������ӽ������пͻ������Ӷ��ѹرգ����ӽ��������˳�������
	 * 3) �鿴�����ļ��е�������(ioctl_quick_abort)�������������� 0 ��
	 *    �ӽ��������˳�������
	 * 4) �����пͻ������ӹرպ���˳�
	 * @param ncleints {size_t} ��ǰ���ӵĿͻ��˸���
	 * @param nthreads {size_t} ��ǰ�̳߳��з�æ�Ĺ����̸߳���
	 * @return {bool} ���� false ��ʾ��ǰ�ӽ��̻������˳��������ʾ��ǰ
	 *  �ӽ��̿����˳���
	 */
	bool proc_exit_timer(size_t nclients, size_t nthreads);

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

	// redis ��Ⱥ����
	acl::redis_client_cluster* redis_;

	void Service(int type, const char* path, http_handler_t fn);
};
