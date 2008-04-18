#ifndef _limadaemon_H
#define _limadaemon_H 1
#include <grace/daemon.h>
#include <grace/configdb.h>
#include <grace/thread.h>
#include <grace/httpd.h>
#include "limadb.h"

//  -------------------------------------------------------------------------
/// Implementation template for application config.
//  -------------------------------------------------------------------------
typedef configdb<class limadaemonApp> appconfig;

//  -------------------------------------------------------------------------
/// Watcher thread for the incoming message queue.
//  -------------------------------------------------------------------------
class inqueuewatch : public thread
{
public:
						 inqueuewatch (class limadaemonApp *papp);
						~inqueuewatch (void);
				
	void				 run (void);
	bool				 checkqueue (const string &listname);
	
protected:
	class limadaemonApp	&app;
};

//  -------------------------------------------------------------------------
/// Watcher thread for the outgoing message queue.
//  -------------------------------------------------------------------------
class outqueuewatch : public thread
{
public:
						 outqueuewatch (class limadaemonApp *papp);
						~outqueuewatch (void);
					
	void				 run (void);
	bool				 checkqueue (const string &);
	
protected:
	class limadaemonApp	&app;
};

//  -------------------------------------------------------------------------
/// Thread-safe database for session-related data storage with
/// auto-expiration.
//  -------------------------------------------------------------------------
class limasessiondb : public thread
{
public:
					 limasessiondb (void);
					~limasessiondb (void);
					
	statstring		*createsession (const string &user, bool moderator,
									bool owner);
	void			 dropsession (const statstring &sessid);
	bool			 exists (const statstring &sessid);
	
	value			*getsession (const statstring &);
	void			 setsession (const statstring &, const value &);
	
	void			 run (void);
	
protected:
	lock<value>		 db;
};

				 
//  -------------------------------------------------------------------------
/// Main daemon class.
//  -------------------------------------------------------------------------
class limadaemonApp : public daemon
{
public:
		 				 limadaemonApp (void);
		 				~limadaemonApp (void);
		 	
	int					 main (void);
	
	bool				 sendmail (const string &sender,
								   const value &rcptlist,
								   const string &message);
	
	limadb				 listdb;
	scriptparser		 script;
	httpd				 srv;
	class inqueuewatch	 inq;
	class outqueuewatch	 outq;
	limasessiondb		 sessiondb;
	
protected:
	bool				 confLog (config::action act, keypath &path,
								  const value &nval, const value &oval);

	appconfig			 conf;
};

#define DEF_SERVERPAGE(xclass,xurl) \
	class xclass : public serverpage \
	{ \
	public: \
		xclass (limadaemonApp *papp) \
			: serverpage (papp->srv, xurl), app (*papp) {} \
		~xclass (void) {} \
		int execute (value &, value &, string &, value &); \
	protected: \
		limadaemonApp &app; \
	}

// Serverpage definitions
DEF_SERVERPAGE (iflogin, "*/login");
DEF_SERVERPAGE (iflogout, "*/logout");
DEF_SERVERPAGE (ifsettings, "*/settings");
DEF_SERVERPAGE (ifmsgmod, "*/msgmod");
DEF_SERVERPAGE (ifusermod, "*/usermod");
DEF_SERVERPAGE (ifuserlist, "*/userlist");
DEF_SERVERPAGE (ifmenu, "*/menu");
DEF_SERVERPAGE (ifnewpass, "*/newpass");
DEF_SERVERPAGE (ifusered, "*/usered");
DEF_SERVERPAGE (iftemplate, "*/template");

//  -------------------------------------------------------------------------
/// Component for extracting the listid from the base url.
//  -------------------------------------------------------------------------
class listidextractor : public httpdobject
{
public:
					 listidextractor (limadaemonApp *papp)
					 	: httpdobject (papp->srv, "*")
					 {
					 }
					~listidextractor (void)
					 {
					 }
				 
	int				 run (string &uri, string &postbody, value &inhdr,
						  string &out, value &outhdr, value &env,
						  tcpsocket &s);
};

//  -------------------------------------------------------------------------
/// Component for sending out image files.
//  -------------------------------------------------------------------------
class imageshare : public httpdobject
{
public:
					 imageshare (limadaemonApp *papp)
					 	: httpdobject (papp->srv, "*.png")
					 {
					 }
					~imageshare (void)
					 {
					 }
					 
	int				 run (string &uri, string &postbody, value &inhdr,
						  string &out, value &outhdr, value &env,
						  tcpsocket &s);
};

//  -------------------------------------------------------------------------
/// Component for intercepting cookie-headers containing a session-id,
/// will load session data into the httpdobject environment.
//  -------------------------------------------------------------------------
class cookiecutter : public httpdobject
{
public:
					 cookiecutter (limadaemonApp *papp)
					 	: httpdobject (papp->srv, "*"), sdb (papp->sessiondb)
					 {
					 }
					~cookiecutter (void)
					 {
					 }
					 
	int				 run (string &uri, string &postbody, value &inhdr,
						  string &out, value &outhdr, value &env,
						  tcpsocket &s);
						  
protected:
	limasessiondb	&sdb;
};

#endif
