#include "stdafx.h"
#include "http_servlet.h"

http_servlet::http_servlet(http_handlers_t* handlers,
	acl::socket_stream* stream, acl::session* session)
: acl::HttpServlet(stream, session)
, handlers_(handlers)
{
}

http_servlet::~http_servlet(void)
{
}

bool http_servlet::doOther(HttpRequest&, HttpResponse& res, const char* method)
{
	res.setStatus(400);
	res.setContentType("text/xml; charset=utf-8");
	// ���� http ��Ӧ��
	acl::string buf;
	buf.format("<root error='unkown request method %s' />\r\n", method);
	res.write(buf);
	res.write(NULL, 0);
	return false;
}

bool http_servlet::doGet(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_get, req, res);
}

bool http_servlet::doPost(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_post, req, res);
}

bool http_servlet::doHead(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_head, req, res);
}

bool http_servlet::doPut(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_put, req, res);
}

bool http_servlet::doPatch(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_patch, req, res);
}

bool http_servlet::doConnect(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_connect, req, res);
}

bool http_servlet::doPurge(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_purge, req, res);
}

bool http_servlet::doDelete(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_delete, req, res);
}

bool http_servlet::doOptions(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_options, req, res);
}

bool http_servlet::doProfind(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_profind, req, res);
}

bool http_servlet::doWebsocket(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_websocket, req, res);
}

bool http_servlet::doUnknown(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_unknown, req, res);
}

bool http_servlet::doError(HttpRequest& req, HttpResponse& res)
{
	return doService(http_handler_error, req, res);
}

bool http_servlet::doService(int type, HttpRequest& req, HttpResponse& res)
{
	// �����Ҫ http session ���ƣ��������ע�ͣ�����Ҫ��֤
	// �� master_service.cpp �ĺ��� thread_on_read �����õ�
	// memcached ������������
	/*
	const char* sid = req.getSession().getAttribute("sid");
	if (*sid == 0) {
		req.getSession().setAttribute("sid", "xxxxxx");
	}
	sid = req.getSession().getAttribute("sid");
	*/

	// �����Ҫȡ������� cookie �������ע��
	/*
	$<GET_COOKIES>
	 */

	if (type < http_handler_get || type >= http_handler_max) {
		logger_error("invalid type=%d", type);
		return false;
	}

	res.setKeepAlive(req.isKeepAlive());
	bool keep = req.isKeepAlive();

	const char* path = req.getPathInfo();
	if (path == NULL || *path == 0) {
		res.setStatus(400);
		acl::string buf("404 bad request\r\n");
		res.setContentLength(buf.size());
		return res.write(buf.c_str(), buf.size()) && keep;
	}

	size_t len = strlen(path);
	acl::string buf(path);
	if (path[len - 1] != '/') {
		buf += '/';
	}
	buf.lower();

	std::map<acl::string, http_handler_t>::iterator it
		= handlers_[type].find(buf);

	if (it != handlers_[type].end()) {
		return it->second(req, res) && keep;
	}

	res.setStatus(404);
	buf  = "404 ";
	buf += path;
	buf += " not found\r\n";
	res.setContentLength(buf.size());
	return res.write(buf.c_str(), buf.size()) && keep;
}
