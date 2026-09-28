#include "Turtle.h"
#include "Turtle.brc"


#ifdef PLATFORM_POSIX
#include <sys/wait.h>
#endif

#define LLOG(x)     LOG(x)
#define LDUMP(x)    DUMP(x)
#define LTIMING(x)

namespace Upp {

static Vector<int> sChildPids;

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
	return HttpResponse(s, h.scgi, 200, "OK", "text/html", html);
}

static void sUpdateChildList()
{
//#ifdef PLATFORM_POSIX
//	int i = 0;
//	while(i < sChildPids.GetCount()) {
//		if(sChildPids[i] && waitpid(sChildPids[i], 0, WNOHANG | WUNTRACED) > 0) {
//			TurtleServer::WhenTerminate(sChildPids[i]);
//			sChildPids.Remove(i);
//		}
//		else ++i;
//	}
//#endif
}

void TurtleServer::Broadcast(int signal)
{
//#ifdef PLATFORM_POSIX
//	if(getpid() == mainpid)
//		for(int i = 0; i < sChildPids.GetCount(); i++)
//			kill(sChildPids[i], signal);
//#endif
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

	for(;;) {
		sUpdateChildList();
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
		websocket.NonBlocking();
		int retries = 0;
		while(!websocket.Accept(ws_port==0?server:ws_server)) {
			Sleep(20);
			if (++retries>100) {
				return false; // Stop it from hanging forever
			}
		}
		LLOG("Received Web Socket connection on socket "<<websocket.GetSOCKET()<< " from " << websocket.GetPeerAddr());
		
//#ifdef PLATFORM_POSIX
//		if(sChildPids.GetCount() >= connection_limit)
//			continue;
//		if(debugmode)
//			break;
//		int newpid = fork();
//		if(!newpid)
//			break; // Does not get here process has already terminated
//		else {
//			LLOG("Process forked. Pid: " << newpid);
//			sChildPids.Add(newpid);
//			WhenConnect(newpid, websocket.GetPeerAddr());
//			continue;
//		}
//#else
		break;
//#endif
	}

	server.Close();
	stat_started = GetSysTime();
	return true;
}
// Add extra controls to start and stop a web session for multiserver applications
void TurtleServer::InitSession()
{
	Ctrl::GlobalBackBuffer();
	Ctrl::InitTimer();

#ifdef PLATFORM_POSIX
	SetStdFont(ScreenSans(12)); //FIXME general handling
#endif
	ChStdSkin();

	#ifdef USE_MULTI_TURTLE
	DesktopRect().Color(Cyan());
	DesktopRect().SetRect(0, 0, DesktopSize.cx, DesktopSize.cy);
	SetDesktop(Desktop());
	
	stat_started = GetSysTime();
	while(!IsWaitingEvent())
		GuiSleep(10);

	ProcessEvents();
	#else
	NEVER();
	#endif
}

// Add extra controls to start and stop a web session for multiserver applications
bool TurtleServer::ConnectSession(int port,const char *hostUrl,const char *webName)
{
	LLOG("Connect session");
	#ifdef USE_MULTI_TURTLE
	socket.Connect("127.0.0.1",port); // Connect to inter process master
	if (socket.IsOpen() && !socket.IsEof()) {
		Ctrl::port = port;

		socket.Timeout(2000); // TODO: Not quite ideal way to make quit work..
		for(;;) {
			if(quit)
				return false;
			HttpHeader http;
			if(http.Read(socket)) {
				LLOG("Accepting, header read");
				if(websocket.WebAccept(socket, http)) {
					LLOG("Accepted, header read");
					InitSession();
					return true;
				}
				LLOG("Sending HTML");
				String html = String(turtle_html,turtle_html_length);
				html.Replace("%%host%%", (String)"ws://" + hostUrl);
				HttpResponse(socket,http.scgi,200,"OK","text/html",html,webName?webName:"");
			}
			if (!socket.IsOpen() || socket.IsEof())
				break;
			if (socket.IsError())
				socket.ClearError();
		}
	}
	if (socket.IsOpen()) socket.Close();
	#else
	NEVER();
	#endif
	return false;
}

// Add extra controls to start and stop a web session for multiserver applications
String GetTurtleHtml()
{
	return String(turtle_html, turtle_html_length);
}

}
