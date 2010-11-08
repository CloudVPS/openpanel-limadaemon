// This file is part of OpenPanel - The Open Source Control Panel
// OpenPanel is free software: you can redistribute it and/or modify it 
// under the terms of the GNU General Public License as published by the Free 
// Software Foundation, using version 3 of the License.
//
// Please note that use of the OpenPanel trademark may be subject to additional 
// restrictions. For more information, please visit the Legal Information 
// section of the OpenPanel website on http://www.openpanel.com/

#include "limadaemon.h"
#include "limadb.h"
#include "limatools.h"
#include "paths.h"
#include <grace/fswatch.h>

APPOBJECT(limadaemonApp);

//  =========================================================================
/// Constructor.
/// Calls daemon constructor, initializes the configdb.
//  =========================================================================
limadaemonApp::limadaemonApp (void)
	: daemon ("com.openpanel.svc.limadaemon"),
	  conf (this),
	  listdb (PATH_LIMADB),
	  inq (this),
	  outq (this)
{
}

//  =========================================================================
/// Destructor.
//  =========================================================================
limadaemonApp::~limadaemonApp (void)
{
}

//  =========================================================================
/// Main method.
//  =========================================================================
int limadaemonApp::main (void)
{
	string conferr; ///< Error return from configuration class.
	
	// Add watcher value for event log. System will daemonize after
	// configuration was validated.
	conf.addwatcher ("system/eventlog", &limadaemonApp::confLog);
	
	// Load will fail if watchers did not valiate.
	if (! conf.load ("com.openpanel.svc.limadaemon", conferr))
	{
		ferr.printf ("%% Error loading configuration: %s\n", conferr.str());
		return 1;
	}
	
	log (log::info, "main", "Application started");
	
	string sc = fs.load ("rsrc:lima.thtml");
	if (! sc)
	{
		ferr.printf ("%% Error loading template script\n");
	}
	fs.save ("sc", sc);
	script.build (sc);
	
	limadb mydb (PATH_LIMADB);
	
	imageshare img (this);
	listidextractor ex (this);
	cookiecutter icook (this);
	iflogin ilogin (this);
	ifnewpass inewp (this);
	iflogout ilogout (this);
	ifsettings ifs (this);
	ifmsgmod ifmsg (this);
	ifusermod ifumod (this);
	ifusered iusred (this);
	ifmsgmod ifmmod (this);
	ifmenu ifmnu (this);
	iftemplate iftmpl (this);

	sessiondb.spawn ();
	inq.spawn ();
	outq.spawn ();
	srv.listento (1888);
	srv.start ();
	
	while (true) sleep (1);
	
	stoplog();
	return 0;
}

//  =========================================================================
/// Configuration watcher for the event log.
//  =========================================================================
bool limadaemonApp::confLog (config::action act, keypath &kp,
							  const value &nval, const value &oval)
{
	string tstr;
	
	switch (act)
	{
		case config::isvalid:
			// Check if the path for the event log exists.
			tstr = strutil::makepath (nval.sval());
			if (! tstr.strlen()) return true;
			if (! fs.exists (tstr))
			{
				ferr.printf ("%% Event log path %s does not exist",
							 tstr.str());
				return false;
			}
			return true;
			
		case config::create:
			// Set the event log target and daemonize.
			fout.printf ("%% Event log: %s\n\n", nval.cval());
			addlogtarget (log::file, nval.sval(), 0xff, 1024*1024);
			daemonize();
			return true;
	}
	
	return false;
}


// ==========================================================================
// CONSTRUCTOR inqueuewatch
// ==========================================================================
inqueuewatch::inqueuewatch (limadaemonApp *papp)
	: app (*papp)
{
}

// ==========================================================================
// DESTRUCTOR inqueuewatch
// ==========================================================================
inqueuewatch::~inqueuewatch (void)
{
}

// ==========================================================================
// METHOD inqueuewatch::run
// ==========================================================================
void inqueuewatch::run (void)
{
	// The sleeptime counter is set at a relaxed pace to begin with, we will
	// increase it (to a set maximum) on every round that leads to no
	// activity and decrease it (to a minimum of 1) for every round that
	// had work.
	int sleeptime = 5;
	
	app.log (log::info, "inqueue", "Starting thread");
	
	// Set up watcher. Do mind that on the first round we'll need to
	// include every directory for a scan, could be interesting.
	fswatch watcher (PATH_INQUEUE, "cur");
	
	// List of files to actually scan.
	value scanlist = fs.ls (PATH_INQUEUE);
	
	while (true)
	{
		bool didwork = false;
		
		foreach (list, scanlist)
		{
			if (checkqueue (list.id().sval()))
				didwork = true;
		}
		
		if (didwork && (sleeptime > 1)) sleeptime--;
		else if ((!didwork) && (sleeptime < 5)) sleeptime++;
		
		value ev = waitevent (sleeptime * 1000);
		if (ev && (ev["cmd"] == "shutdown"))
		{
			break;
		}
		
		value changeset = watcher.listchanges ();
		changeset.savexml ("cs-inqueue.xml");
		
		scanlist.clear();
		scanlist << changeset["modified"];
		scanlist << changeset["created"];
		
		app.log (log::info, "inqueue", "New round with %i changed queues",
				 scanlist.count());
	}
	
	// we broke out, got a shutdown request obviously.
}

// ==========================================================================
// METHOD inqueuewatch::checkqueue
// ==========================================================================
bool inqueuewatch::checkqueue (const string &listname)
{
	bool workdone = false;
	string qpath = PATH_INQUEUE;
	qpath.printf ("/%s/cur", listname.str());
	
	app.log (log::info, "inqueue", "Checking queue for <%s>", listname.str());

	if (! app.listdb.listexists (listname)) return false;
	
	mailinglist list (app.listdb, listname);
	bool needsmod = (list.moderatedposts () == POST_MODERATED);
	
	value qfiles = fs.ls (qpath);
	
	foreach (qfile, qfiles)
	{
		if (qfile.id().sval().globcmp ("*.xml"))
		{
			app.log (log::info, "inqueue", "Checking <%s/%s>", listname.str(),
					 qfile.name());
					 
			// We don't need to look at the parsed headers at this
			// point, making me think it may be more beneficial to
			// only use headers in the moderation queue.
			string fid = qfile.id().sval().copyuntil (".xml");
			
			string oldxml, oldmsg, newxml, newmsg, newdir;
			
			oldxml.printf ("%s/%s/cur/%s.xml",
						   PATH_INQUEUE, listname.str(), fid.str());
			oldmsg.printf ("%s/%s/cur/%s.msg",
						   PATH_INQUEUE, listname.str(), fid.str());
						   
			if (needsmod)
			{
				newxml.printf ("%s/%s/cur/%s.xml",
							   PATH_MODQUEUE, listname.str(), fid.str());
				newmsg.printf ("%s/%s/cur/%s.msg",
							   PATH_MODQUEUE, listname.str(), fid.str());
				newdir.printf ("%s/%s", PATH_MODQUEUE, listname.str());
			
			}
			else
			{
				newxml.printf ("%s/%s/cur/%s.xml",
							   PATH_OUTQUEUE, listname.str(), fid.str());
				newmsg.printf ("%s/%s/cur/%s.msg",
							   PATH_OUTQUEUE, listname.str(), fid.str());
				newdir.printf ("%s/%s", PATH_OUTQUEUE, listname.str());
			}
			
			if (! fs.exists (newdir))
			{
				fs.mkdir (newdir);
			}
			
			newdir.printf ("/cur");
			if (! fs.exists (newdir))
			{
				fs.mkdir (newdir);
			}
			
			string dstbase;
			
			if (! fs.mv (oldmsg, newmsg))
			{
				// whine
				app.log (log::error, "inqueue", "Error moving queue file "
						 "<%s> to <%s>", oldmsg.str(), newmsg.str());
				continue;
			}
			
			if (! fs.mv (oldxml, newxml))
			{
				// also whine
				// but roll back the earlier move.
				app.log (log::error, "inqueue", "Error moving queue file "
						 "<%s> to <%s>", oldxml.str(), newxml.str());
				fs.mv (newmsg, oldmsg);
				continue;
			}
			
			workdone = true;
		}
	}
	
	return workdone;
}

// ==========================================================================
// CONSTRUCTOR outqueuewatch
// ==========================================================================
outqueuewatch::outqueuewatch (limadaemonApp *papp)
	: app (*papp)
{
}

// ==========================================================================
// DESTRUCTOR outqueuewatch
// ==========================================================================
outqueuewatch::~outqueuewatch (void)
{
}

// ==========================================================================
// METHOD outqueuewatch::run
// ==========================================================================
void outqueuewatch::run (void)
{
	int sleeptime = 5;
	fswatch watcher (PATH_OUTQUEUE, "cur");
	
	app.log (log::info, "outqueue", "Starting thread");
	
	// List of files to actually scan.
	value scanlist = fs.ls (PATH_OUTQUEUE);
	
	while (true)
	{
		bool didwork = false;
		
		foreach (list, scanlist)
		{
			if (checkqueue (list.id().sval()))
				didwork = true;
		}
		
		if (didwork && (sleeptime > 1)) sleeptime--;
		else if ((!didwork) && (sleeptime < 5)) sleeptime++;
		
		value ev = waitevent (sleeptime * 1000);
		if (ev && (ev["cmd"] == "shutdown"))
		{
			break;
		}
		
		value changeset = watcher.listchanges ();
		scanlist.clear();
		scanlist << changeset["modified"];
		scanlist << changeset["created"];
	}
	
	// we broke out, got a shutdown request obviously.
}

// ==========================================================================
// METHOD outqueuewatch::checkqueue
// ==========================================================================
bool outqueuewatch::checkqueue (const string &listname)
{
	bool workdone = false;
	value rcptlist;
	
	string qpath = PATH_OUTQUEUE;
	qpath.printf ("/%s/cur", listname.str());
	
	if (! app.listdb.listexists (listname)) return false;
	app.listdb.loadlist (listname);
	
	mailinglist list (app.listdb, listname);
	bool needsmod = list.moderatedposts ();
	
	value ldump = list.dump ();
	ldump.savexml ("list.xml");
	
	value qfiles = fs.ls (qpath);
	foreach (qfile, qfiles)
	{
		if (qfile.id().sval().globcmp ("*.xml"))
		{
			app.log (log::info, "outqueue", "Checking <%s>", qfile.name());
			
			string fid = qfile.id().sval().copyuntil (".xml");
			string fmsg = qfile["path"].sval().copyuntil (".xml");
			fmsg.strcat (".msg");
			
			string msg = fs.load (fmsg);
			if (! msg.strlen())
			{
				app.log (log::error, "outqueue", "Error loading <%s>: empty",
						 fmsg.str());
				// FIXME
				continue;
			}
			
			if (! rcptlist.count())
			{
				rcptlist = list.listmembers();
			}
			
			if (! rcptlist.count())
			{
				app.log (log::error, "outqueue", "Error queueing message, "
						 "recipient-list for <%s> seems empty",
						 listname.str());
				// whine
				break;
			}
			
			string listdomain = listname;
			string listuser = listdomain.cutat ('@');
			
			string sender;
			sender.printf ("%s+bounce@%s", listuser.str(), listdomain.str());
			
			if (! app.sendmail (sender, rcptlist, msg))
			{
				// FIXME: send horrible error
				continue;
			}
			
			if (! fs.rm (qfile["path"]))
			{
				app.log (log::error, "outq", "Error removing queued file "
						 "<%s>", qfile["path"].cval());
				// FIXME: bitch and whine, shut down possibly?
			}
			if (! fs.rm (fmsg))
			{
				app.log (log::error, "outq", "Error removing queued file "
						 "<%s>", fmsg.str());
				// FIXME: likewise
			}
		}
	}
}

// ==========================================================================
// METHOD limadaemonApp::sendmail
// ==========================================================================
bool limadaemonApp::sendmail (const string &sender,
							  const value &rcpt,
							  const string &data)
{
	string err;
	bool res = limatools::sendmail (sender, rcpt, data, err, true);
	if (! res)
	{
		log (log::error, "sendmail", "%s", err.str());
	}
	
	return res;
}

// ==========================================================================
// METHOD ifusermod::execute
// ==========================================================================
int ifusermod::execute (value &env, value &argv, string &out,
						value &outhdr)
{
	// Input parameters:
	// - mode: empty, 'approve' or 'reject'.
	// - user: when mode is not empty, the email address to approve/reject.
	string in_mode = argv["mode"];
	string in_user = argv["user"];
	string my_user = env["user"];
	string in_list = env["listid"];

	value senv = argv;
	senv << env;
	
	
	if (! my_user)
	{
		string nloc;
		nloc.printf ("/%s/login", in_list.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthenticated");
		return 303;
	}

	if (! app.listdb.listexists (in_list))
	{
		app.log (log::error, "ui", "Error handling request for list "
				 "<%S>", in_list.str());
		app.script.run (senv, out, "err:listnotfound");
		return 200;
	}
	
	mailinglist mlist (app.listdb, in_list);
	
	if (in_mode == "approve")
	{
		if (! mlist.memberexists (in_user))
		{
			app.log (log::error, "ui", "Error handling request for "
					 "unknown member <%S> for list <%S>", in_user.str(),
					 in_list.str());
			app.script.run (senv, out, "err:usernotfound");
			return 200;
		}
		
		// argv["user"] is a provided argument, env["user"] is set by
		// the httpdbasicauth class when it gets a positive result from
		// our authenticator.
		app.log (log::info, "ui", "Approved membership <%s> by <%s>",
				 in_user.str(), env["user"].cval());
		
		// Approve the member.
		mlist.approvemember (in_user);
	}
	else if (in_mode == "reject")
	{
		if (! mlist.memberexists (in_user))
		{
			app.log (log::error, "ui", "Error handling request for "
					 "unknown member <%S> for list <%S>", in_user.str(),
					 in_list.str());
			app.script.run (senv, out, "err:usernotfound");
			return 200;
		}

		app.log (log::info, "ui", "Rejected membership <%s> by <%s>",
				 in_user.str(), env["user"].cval());
				 
		mlist.disapprovemember (in_user);
	}
	
	senv["members"] = mlist.listmembermodq ();
	
	string txml = senv.toxml ();
	
	app.script.run (senv, out, "moderateusr");
	return 200;
}

// ==========================================================================
// METHOD ifmenu::execute
// ==========================================================================
int ifmenu::execute (value &env, value &argv, string &out, value &outhdr)
{
	value senv = argv;
	senv << env;
	
	string in_user = env["user"];
	string in_listid = env["listid"];
	
	// The env["user"] value is set by the cookiecutter object if it
	// intercepts an authenticated session-id.
	if (! in_user)
	{
		// Not authenticated, redirect to login.
		string nloc;
		nloc.printf ("/%s/login", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthenticated");
		return 303;
	}

	// Display the menu section from the template script.
	app.script.run (senv, out, "menu");
	return 200;
}

// ==========================================================================
// METHOD listidextractor::run
// ==========================================================================
int listidextractor::run (string &uri, string &postbody, value &inhdr,
						  string &out, value &outhdr, value &env, tcpsocket &s)
{
	value pathelm = strutil::split (uri, '/');
	env["listid"] = pathelm[1];
	return 0;
}

// ==========================================================================
// METHOD ifsettings::execute
// ==========================================================================
int ifsettings::execute (value &env, value &argv, string &out,
						 value &outhdr)
{
	value senv = argv;
	senv << env;
	
	string in_mode = argv["mode"];
	string in_user = env["user"];
	string in_listid = env["listid"];
	
	// The env["user"] value is set by the cookiecutter object if it
	// intercepts an authenticated session-id.
	if (! in_user)
	{
		// Not authenticated, redirect to login.
		string nloc;
		nloc.printf ("/%s/login", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthenticated");
		return 303;
	}
	
	// Make sure the list exists.
	if (! app.listdb.listexists (in_listid))
	{
		app.log (log::error, "ui", "Error handling request for list "
				 "<%S>", in_listid.str());
		app.script.run (senv, out, "err:listnotfound");
		return 200;
	}
	
	mailinglist mlist (app.listdb, in_listid);
	string listowner = mlist.owner ();
	
	// This page is only available to the list owner.
	if (env["user"].sval() != listowner)
	{
		app.log (log::error, "ui", "Access-attempt by non-owner <%S> for "
				 "list <%s>", env["user"].cval(), in_listid.cval());
		string nloc;
		nloc.printf ("/%s/menu", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthorized");
		return 303;
	}
	
	// Handle followup-requests (mode=submit).
	if (argv["mode"] == "submit")
	{
		try
		{
			if (argv["listname"].sval())
				mlist.name (argv["listname"]);
			
			mlist.subjecttag (argv["subjecttag"]);
			
			if (argv["replyto"] == "1")
			{
				mlist.replytoheader (true);
			}
			else
			{
				mlist.replytoheader (false);
			}
			
			if (argv["moderatedsubs"] == "1")
			{
				mlist.moderatedsubs (true);
			}
			else
			{
				mlist.moderatedsubs (false);
			}
			
			mlist.moderatedposts ((postrestriction_t) argv["moderatedposts"].ival());
		}
		catch (exception e)
		{
			::printf ("Caught: %s\n", e.description);
		}
	}

	if (mlist.replytoheader()) senv["check_replyto"] = "checked";
	senv["listname"] = mlist.name ();
	senv["subjecttag"] = mlist.subjecttag ();
	if (mlist.moderatedsubs()) senv["check_moderatedsubs"] = "checked";
	switch (mlist.moderatedposts())
	{
		case POST_OPEN :
			senv["sel_open"] = "selected"; break;
		
		case POST_MEMBERSONLY :
			senv["sel_membersonly"] = "selected"; break;
		
		default :
			senv["sel_moderated"] = "selected"; break;
	}
	
	app.script.run (senv, out, "settings");
	return 200;
}

// ==========================================================================
// METHOD ifmsgmod::execute
// ==========================================================================
int ifmsgmod::execute (value &env, value &argv, string &out,
					   value &outhdr)
{
	value senv = argv;
	senv << env;
	
	string in_listid = env["listid"];
	string user = env["user"];
	
	// The env["user"] value is set by the cookiecutter object if it
	// intercepts an authenticated session-id.
	if (! user)
	{
		// Not authenticated, redirect to login.
		string nloc;
		nloc.printf ("/%s/login", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthenticated");
		return 303;
	}
	
	// Verify the list exists.
	if (! app.listdb.listexists (in_listid))
	{
		app.log (log::error, "ui", "Error handling request for list "
				 "<%S>", in_listid.str());
		app.script.run (senv, out, "err:listnotfound");
		return 200;
	}
	
	mailinglist mlist (app.listdb, in_listid);
	listmember &memb = mlist.getmember (user);
	
	// Check the member for moderator access.
	if (! memb.moderator ())
	{
		string nloc;
		nloc.printf ("/%s/menu", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthorized");
		return 303;
	}
	
	string qpath = PATH_MODQUEUE;
	qpath.printf ("/%s/cur", in_listid.str());

	string ofnxml;
	string ofnmsg;
	
	string nfnxml;
	string nfnmsg;

	// Handle followup-requests that approve/reject an individual message.
	if ((argv["mode"] == "approve") || (argv["mode"] == "reject"))
	{
		string in_msgid = argv["msgid"];
		ofnxml = ofnmsg = qpath;
		ofnxml.printf ("/%s.xml", in_msgid.str());
		ofnmsg.printf ("/%s.msg", in_msgid.str());
		if ((! fs.exists (ofnxml)) || (! fs.exists (ofnmsg)))
		{
			app.log (log::error, "msgmod", "Request for message with id "
					 "<%S> failed: not found", in_msgid.str());
		}
		else
		{
			if (argv["mode"] == "approve")
			{
				string npath = PATH_OUTQUEUE;
				npath.printf ("/%s/cur", in_listid.str());
				
				nfnxml = nfnmsg = npath;
				nfnxml.printf ("/%s.xml", in_msgid.str());
				nfnmsg.printf ("/%s.msg", in_msgid.str());
				
				if (! fs.mv (ofnmsg, nfnmsg))
				{
					app.log (log::error, "msgmod", "Error moving message "
							 "<%s> to outqueue", in_msgid.str());
				}
				else if (! fs.mv (ofnxml, nfnxml))
				{
					app.log (log::error, "msgmod", "Error moving message "
							 "<%s> headers to outqueue", in_msgid.str());
				}
			}
			else
			{
				fs.rm (ofnxml);
				fs.rm (ofnmsg);
			}
		}
	}

	// Get a list of messages with subject line	
	value msgq = limatools::scandir (qpath);
	
	senv["msgs"] = msgq;
	
	app.script.run (senv, out, "moderatemsg");
	return 200;
}

// ==========================================================================
// CONSTRUCTOR limasessiondb
// ==========================================================================
limasessiondb::limasessiondb (void) : thread ("sessiondb")
{
}

// ==========================================================================
// DESTRUCTOR limasessiondb
// ==========================================================================
limasessiondb::~limasessiondb (void)
{
}

// ==========================================================================
// METHOD limasessiondb::createsession
// ==========================================================================
statstring *limasessiondb::createsession (const string &user, bool m, bool o)
{
	returnclass (statstring) res retain;
	value dt;
	
	res = strutil::uuid ();
	dt["user"] = user;
	dt["priv_moderator"] = m ? 1 : 0;
	dt["priv_owner"] = o ? 1 : 0;
	dt["ts"] = (unsigned int) kernel.time.now ();
	
	setsession (res, dt);
	return &res;
}

// ==========================================================================
// METHOD limasessiondb::dropsession
// ==========================================================================
void limasessiondb::dropsession (const statstring &sessid)
{
	exclusivesection (db)
	{
		if (db.exists (sessid)) db.rmval (sessid);
	}
}

// ==========================================================================
// METHOD limasessiondb::getsession
// ==========================================================================
value *limasessiondb::getsession (const statstring &sessid)
{
	returnclass (value) res retain;
	
	sharedsection (db)
	{
		if (db.exists (sessid))
		{
			res = db[sessid];
			db[sessid]["ts"] = (unsigned int) kernel.time.now ();
		}
	}
	
	return &res;
}

// ==========================================================================
// METHOD limasessiondb::setsession
// ==========================================================================
void limasessiondb::setsession (const statstring &sessid, const value &dt)
{
	exclusivesection (db)
	{
		db[sessid] = dt;
	}
}

// ==========================================================================
// METHOD limasessiondb::exists
// ==========================================================================
bool limasessiondb::exists (const statstring &sessid)
{
	bool result = false;
	
	sharedsection (db)
	{
		result = db.exists (sessid);
	}
	
	return result;
}

// ==========================================================================
// METHOD limasessiondb::run
// ==========================================================================
void limasessiondb::run (void)
{
	unsigned int now;
	
	// Main loop. Clean out expired sessions every 10 minutes.
	while (true)
	{
		sleep (600);
		now = kernel.time.now ();
		value outlist;
		
		exclusivesection (db)
		{
			foreach (n, db)
			{
				if ((now - n["ts"].uval()) > 3600)
				{
					outlist[n.id()] = true;
				}
			}
			
			foreach (n, outlist)
			{
				db.rmval (n.id());
			}
		}
	}
}

// ==========================================================================
// METHOD cookiecutter::run
// ==========================================================================
int cookiecutter::run (string &uri, string &postbody, value &inhdr,
					   string &out, value &outhdr, value &env,
					   tcpsocket &s)
{
	// Check for a cookie-header containing a SESSID value
	if (! inhdr.exists ("Cookie")) return 0;
	string ck = inhdr["Cookie"];
	if (ck.strstr ("SESSID=") < 0) return 0;
	
	// Extract the session-id the lazy way, or bail out.
	string sid = strutil::regexp (ck, "s/.*SESSID=//;s/ ?\\;.*//");
	if (! sdb.exists (sid)) return 0;
	
	// Extract the session data from the session database.
	value sdat = sdb.getsession (sid);
	
	// Copy session data to environment
	env["sessionid"] = sid;
	env << sdat;
	
	// Fall through to next httpdobject.
	return 0;
}

// ==========================================================================
// METHOD iflogin::execute
// ==========================================================================
int iflogin::execute (value &env, value &argv, string &out, value &outhdr)
{
	statstring listname = env["listid"];
	value senv;
	string user;
	senv << env;
	
	if (argv.exists ("username"))
	{
		senv["username"] = user = argv["username"];
		
		if (app.listdb.listexists (listname))
		{
			mailinglist list (app.listdb, listname);
			if (list.memberexists (user))
			{
				string listowner = list.owner ();
				listmember &memb = list.getmember (user);
				if (memb.verifypassword (argv["password"].sval()))
				{
					statstring cid;
					bool isowner = (user == listowner);
					bool ismod = memb.moderator();
					string ck, loc;
					
					cid = app.sessiondb.createsession (user, ismod, isowner);
					ck.printf ("SESSID=%s", cid.str());
					loc.printf ("/%s/menu", env["listid"].cval());
					
					outhdr["Set-Cookie"] = ck;
					outhdr["Location"] = loc;
					return 301;
				}
			}
		}
	}
	
	app.script.run (senv, out, "login");
	return 200;
}

// ==========================================================================
// METHOD iflogout::execute
// ==========================================================================
int iflogout::execute (value &env, value &argv, string &out, value &outhdr)
{
	statstring listname = env["listid"];
	string loc;

	if (env.exists ("sessionid"))
	{
		app.sessiondb.dropsession (env["sessionid"].sval());
	}
	
	loc.printf ("/%s/login", listname.str());
	outhdr["Location"] = loc;
	
	return 301;
}

// ==========================================================================
// METHOD ifnewpass::execute
// ==========================================================================
int ifnewpass::execute (value &env, value &argv, string &out, value &outhdr)
{
	string listname = env["listid"];
	string user = argv["username"];
	string rndchar = "abcdefghjkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	
	value senv;
	
	senv << argv;
	senv << env;
	
	senv["success"] = 0;
	
	srand (kernel.time.now() ^ kernel.proc.self());
	
	if (user.strlen() && app.listdb.listexists (listname))
	{
		mailinglist list (app.listdb, listname);
		if (list.memberexists (user))
		{
			char c;
			string npass;
			
			for (int i=0; i<12; ++i)
			{
				c = rndchar[rand() % rndchar.strlen()];
				npass.strcat (c);
			}
			
			listmember &memb = list.getmember (user);
			memb.password (npass);
			
			value menv;
			string owner;
			string domainpart;
			string namepart;
			domainpart = listname.copyafter ('@');
			namepart = listname.copyuntil ('@');
			owner.printf ("%s+owner@%s", namepart.str(), domainpart.str());

			menv["listid"] = listname;
			menv["subjecttag"] = list.subjecttag();
			menv["title"] = list.name();
			menv["listname"] = namepart;
			menv["listowner"] = owner;
			menv["domainname"] = domainpart;
			menv["password"] = npass;
			menv["subscriber"] = user;
			
			string msg = limatools::getdefaultmsg (listname, "newpass", menv);
			string err;
			
			if (limatools::sendmail (owner, user, msg, err, false))
			{
				senv["success"] = 1;
			}
		}
	}
	
	app.script.run (senv, out, "newpass");
	return 200;
}

// ==========================================================================
// METHOD ifusered::execute
// ==========================================================================
int ifusered::execute (value &env, value &argv, string &out, value &outhdr)
{
	value senv;
	senv << argv;
	senv << env;
	
	string txml = argv.toxml ();
	::printf ("--usered in--\n%s\n", txml.str());
	
	string in_listid = env["listid"];
	string user = env["user"];
	
	// The env["user"] value is set by the cookiecutter object if it
	// intercepts an authenticated session-id.
	if (! user)
	{
		// Not authenticated, redirect to login.
		string nloc;
		nloc.printf ("/%s/login", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthenticated");
		return 303;
	}
	
	// Verify the list exists.
	if (! app.listdb.listexists (in_listid))
	{
		app.log (log::error, "ui", "Error handling request for list "
				 "<%S>", in_listid.str());
		app.script.run (senv, out, "err:listnotfound");
		return 200;
	}
	
	mailinglist mlist (app.listdb, in_listid);
	listmember &memb = mlist.getmember (user);
	
	// Check the member for moderator access.
	if (! memb.moderator ())
	{
		string nloc;
		nloc.printf ("/%s/menu", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthorized");
		return 303;
	}
	
	try
	{
		if (argv["mode"] == "setmod")
		{
			listmember &tmemb = mlist.getmember (argv["id"]);
			tmemb.moderator (true);
		}
		else if (argv["mode"] == "unsetmod")
		{
			listmember &tmemb = mlist.getmember (argv["id"]);
			tmemb.moderator (false);
		}
		else if (argv["mode"] == "delete")
		{
			mlist.deletemember (argv["id"]);
		}
	}
	catch (exception e)
	{
		app.log (log::warning, "usered", "Punk exception: %s", e.description);
	}
	
	int offset = argv["offset"];
	
	senv["members"] = mlist.listmembers (offset, 30);
	
	app.script.run (senv, out, "usered");
	return 200;
}

// ==========================================================================
// METHOD iftemplate::execute
// ==========================================================================
int iftemplate::execute (value &env, value &argv, string &out, value &outhdr)
{
	value senv;
	senv << argv;
	senv << env;
	
	string in_listid = env["listid"];
	string user = env["user"];
	string in_type = argv["type"];
	
	// The env["user"] value is set by the cookiecutter object if it
	// intercepts an authenticated session-id.
	if (! user)
	{
		// Not authenticated, redirect to login.
		string nloc;
		nloc.printf ("/%s/login", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthenticated");
		return 303;
	}
	
	// Verify the list exists.
	if (! app.listdb.listexists (in_listid))
	{
		app.log (log::error, "ui", "Error handling request for list "
				 "<%S>", in_listid.str());
		app.script.run (senv, out, "err:listnotfound");
		return 200;
	}
	
	mailinglist mlist (app.listdb, in_listid);
	listmember &memb = mlist.getmember (user);
	
	// Check the member for moderator access.
	if (! memb.moderator ())
	{
		string nloc;
		nloc.printf ("/%s/menu", in_listid.str());
		outhdr["Location"] = nloc;
		out.printf ("Unauthorized");
		return 303;
	}
	
	if (argv["mode"] == "submit")
	{
		limatools::setdefaultmsg (in_listid, in_type, argv["message"].sval());
	}
	
	senv["message"] = limatools::getdefaultmsg (in_listid, in_type);
	
	app.script.run (senv, out, "template");
	return 200;
}

// ==========================================================================
// METHOD imageshare::run
// ==========================================================================
int imageshare::run (string &uri, string &postbody, value &inhdr, string &out,
					 value &outhdr, value &env, tcpsocket &s)
{
	string imgname = uri;
	imgname.cropafterlast ('/');
	string imgpath = "rsrc:";
	imgpath += imgname;
	
	if (! fs.exists (imgpath)) return 404;
	
	out = fs.load (imgpath);
	outhdr["Content-type"] = "image/png";
	return 200;
}
