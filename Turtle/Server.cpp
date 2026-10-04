#include "Turtle.h"
#include "Turtle.brc"

#ifdef PLATFORM_POSIX
#include <sys/wait.h>
#endif

#define LLOG(x)    // LOG(x)
#define LDUMP(x)   // DUMP(x)
#define LTIMING(x)

namespace Upp {

static Vector<int> sChildPids;

Image *AppIcon = 0;

static bool sSendTurtleHtml(TcpSocket& s, const String& host, int port)
{
	HttpHeader h;
	if(!h.Read(s))
		return false;
	// Ensure this is not actually a web socket connection that has got mixed up
	String key = h["Sec-Websocket-Version"];
	if(*key) {
		LLOG("received unexpected web socket");
		return false;
	}
	// Send Java script HTML file to capture key / mouse events
	LLOG("Sending Turtle HTML skt:"<<s.GetSOCKET()<<" host:"<<host<<" port:"<<port);
	String html = String(turtle_html, turtle_html_length);
	if(port == 0)
		html.Replace("\"%%host%%\"", "{ noServer: true }");
	else
		html.Replace("%%host%%", Format("ws://%s:%d", host, port));

	if (AppIcon) {
		// Browsers request the favicon before requesting the web socket, thus providing it in advance to prevent this
		int at = html.Find("</head>");
		String icon64 = Base64Encode((const char*)AppIcon->Begin(),AppIcon->GetLength());
		String link = Format("<link rel=\"icon\" type=\"image/x-icon\" href=\"data:image/x-icon;base64,%s\" />",icon64);
		html.Insert(at,link);
	}
	return HttpResponse(s, h.scgi, 200, "OK", "text/html", html);
}

bool TurtleServer::StartSession()
{
	// TODO: See if we can add secure websocket (wss://) support.
	
	LLOG("Connect");

#ifdef PLATFORM_POSIX
	mainpid = getpid();
#endif

	IpAddrInfo ipinfo;
	ipinfo.Execute(ip, html_port, IpAddrInfo::FAMILY_IPV4);

	IpAddrInfo ipinfo_ws;
	ipinfo_ws.Execute(ip, ws_port, IpAddrInfo::FAMILY_IPV4);

	TcpSocket server, ws_server;

#ifdef _DEBUG
	int cnt = 0;
	LLOG("Open HTTP port on " << html_port << " with pid: " << getpid());
	while(!server.Listen(ipinfo, html_port, 5, false, true)) {
		LLOG("Trying to open HTTP port (another process using the same port?) " << ++cnt);
		Sleep(1000);
	}
	if(ws_port != 0) {
		LLOG("Open Web Socket port on " << html_port << " with pid: " << getpid());
		while(!ws_server.Listen(ipinfo_ws, ws_port, 5, false, true)) {
			LLOG("Trying to open Web Socket port (another process using the same port?) " << ++cnt);
			Sleep(1000);
		}
	}
#else
	LLOG("Open HTTP port on " << html_port << " with pid: " << getpid());
	if(!server.Listen(ipinfo, html_port, 5, false, true)) {
		LLOG("Cannot open server socket for listening on HTTP port!");
		Exit(1);
	}
	if(ws_port != 0) {
		LLOG("Open Web Socket port on " << html_port << " with pid: " << getpid());
		if(!ws_server.Listen(ipinfo_ws, ws_port, 5, false, true)) {
			LLOG("Cannot open server socket for listening on Web Socket port!");
			Exit(1);
		}
	}
#endif

	server.Timeout(10000); // Ensure read does not go on forever
	for(;;) {
		if(server.IsError())
			server.ClearError();
		TcpSocket socket;
		if(!socket.Accept(server))
			continue;
		LLOG("Received HTML connection on socket "<<socket.GetSOCKET()<< " from " << socket.GetPeerAddr());
		if (html_port==443) { // Note HTTPS uses port 443, but browsers will normally reject the connection without a certificate (which helps to prevents man in the middle attacks)
			if (socket.StartSSL()) {
				while (socket.SSLHandshake()) {
					Sleep(10); // Note SSLHandshake() will timeout
				}
			}
		}
		if(!sSendTurtleHtml(socket, host, ws_port))
			continue;
		int res, retries = 0;
		for(;;) {
			res = websocket.Accept(ws_port==0?server:ws_server);
			if (res==0) {
				Sleep(20);
				if (++retries>100) {
					return false; // Stop it from hanging forever
				}
			}
			else {
				break;
			}
		}
		if (res == -1)
			continue;
		LLOG("Received Web Socket connection on socket "<<websocket.GetSOCKET()<< " from " << websocket.GetPeerAddr());
		break;
	}

	LLOG("Close HTTP port on " << html_port << " with pid: " << getpid());
	server.Close();
	stat_started = GetSysTime();
	return true;
}

String TurtleServer::GetJavaScript()
{
	return String(turtle_html, turtle_html_length);
}
}
