#include "limadb.h"
#include <grace/filesystem.h>
#include <grace/system.h>

string VALID_LISTNAME="0123456789abcdefghijklmnopqrstuvwxyz"
					  "ABCDEFGHIJKLMNOPQRSTUVWXYZ-.+@_";

// ==========================================================================
// CONSTRUCTOR limadb
// ==========================================================================
limadb::limadb (const string &pbase)
	: DB (dbengine::SQLite)
{
	string dbpath = pbase;
	value cenv;
	bool needscreates = false;
	
	dbpath.strcat ("/lima.db");
	cenv["path"] = dbpath;

	if (! fs.exists (dbpath)) needscreates = true;
	DB.open (cenv);
	
	// If the file did not exist, we'll need to create the
	// database schema. 
	if (needscreates)
	{
		if (! DB.query ("CREATE TABLE lists ("
						"  id integer primary key,"
						"  address char(128),"
						"  subjecttag char(64),"
						"  name char(128),"
						"  moderation_posts integer,"
						"  moderation_subscriptions integer,"
						"  replyto integer,"
						"  owner char(128),"
						"  salt char(128))"))
		{
			throw (tableCreateException());
		}
		
		if (! DB.query ("CREATE TABLE members ("
						"  id integer primary key,"
						"  listid integer,"
						"  address char(128),"
						"  password char(24),"
						"  needsprobe integer,"
						"  approved integer,"
						"  moderator integer)"))
		{
			throw (tableCreateException());
		}
	}
	
	// Attach the table objects to the database tables.
	tlists.attach (DB, "lists");
	tlists.setindexcolumn ("address");
	tmembers.attach (DB, "members");
	tmembers.setindexcolumn ("id");
}

// ==========================================================================
// DESTRUCTOR limadb
// ==========================================================================
limadb::~limadb (void)
{
}

// ==========================================================================
// METHOD limadb::listexists
// ==========================================================================
bool limadb::listexists (const statstring &listid)
{
	return tlists.rowexists (listid);
}

// ==========================================================================
// METHOD limadb::gensalt
// ==========================================================================
string *limadb::gensalt (void)
{
	returnclass (string) res retain;
	
	file f;
	if (! f.openread ("/dev/urandom"))
	{
		//throw (randomDeviceException());
	}
	
	res = f.read (64);
	return &res;
}

// ==========================================================================
// METHOD limadb::createlist
// ==========================================================================
bool limadb::createlist (const statstring &listid, 
						 const string &owner, bool usemoderation,
						 bool usesubmoderation, const string &plistname,
						 const string &psubjecttag)
{
	// Filter format, reject doubles.
	if (!listid.sval().validate (VALID_LISTNAME))
	{
		::fprintf (stderr, "Invalid listame %s\n", listid.str());
		return false;
	}
	if (listexists (listid))
	{
		::fprintf (stderr, "List already exists\n");
		return false;
	}
	
	// Create a 64bits salt.
	string mysalt = gensalt();
	mysalt = mysalt.encode64 ();
	
	string listname = plistname;
	string subjecttag = psubjecttag;
	
	// If no listname is provided, use the 'user'-part of the
	// list's address.
	if (! listname.strlen())
	{
		listname = listid.sval().copyuntil ('@');
	}
	
	// If no subject tag is provided, use the listname.
	if (! subjecttag.strlen())
	{
		subjecttag = listname;
	}
	
	// Fill in database metadata.
	value data;
	data["address"] = listid;
	data["subjecttag"] = subjecttag;
	data["name"] = listname;
	data["moderation_posts"] = (int) usemoderation;
	data["moderation_subscriptions"] = usesubmoderation?1:0;
	data["replyto"] = 0;
	data["owner"] = owner;
	data["salt"] = mysalt;
	
	// Send create statement
	dbquery q (DB);
	q.insertinto (tlists).values (data);
		
	if (! q.execvoid())
	{
		::fprintf (stderr, "SQL query failed\n");
		return false;
	}
	return true;
};

bool limadb::deletelist (const statstring &listid)
{
	dbquery q (DB);
	q.select (tlists["id"]);
	q.where (tlists["address"] == listid.sval());
	value res = q.exec ();
	if (! res.count()) return false;

	dbquery qq (DB);
	qq.deletefrom (tmembers).where (tmembers["listid"] == res[0]["id"].ival());
	qq.execvoid ();
	
	dbquery qqq (DB);
	qqq.deletefrom (tlists).where (tlists["address"] == listid.sval());
	return qqq.execvoid ();
}

// ==========================================================================
// METHOD limadb::loadlist
// ==========================================================================
bool limadb::loadlist (const statstring &listid)
{
	return tlists.rowexists (listid);
}

// ==========================================================================
// CONSTRUCTOR listmember
// ==========================================================================
listmember::listmember (void)
	: list (*(new mailinglist()))
{
	throw (defaultConstructorException());
}

listmember::listmember (const string &paddress, mailinglist *plist)
	: list (*plist), _address (paddress)
{
}

listmember::listmember (listmember *orig)
	: list (orig->list), _address (orig->_address)
{
	delete orig;
}

listmember::listmember (listmember &orig)
	: list (orig.list), _address (orig._address)
{
	lastload = 0;
	load ();
}

// ==========================================================================
// DESTRUCTOR listmember
// ==========================================================================
listmember::~listmember (void)
{
}

// ==========================================================================
// METHOD listmember::load
// ==========================================================================
void listmember::load (void)
{
	// Keep a reasonable delay between re-loads
	time_t now = kernel.time.now();
	if ((now-lastload) < 5) return;
	lastload = now;

	// Convenience references.
	dbengine &DB = list.ldb.DB;
	dbtable &tlists = list.ldb.tlists;
	dbtable &tmembers = list.ldb.tmembers;
	
	// Construct query
	dbquery q (DB);
	q.select (tmembers);
	q.from (tlists, tmembers);
	q.where ((tlists["id"] == tmembers["listid"]) &&
			 (tmembers["address"] == _address.sval()) &&
			 (tlists["address"] == list.id().sval()));

	value v = q.exec();
	mdata = v[0];
}

// ==========================================================================
// METHOD listmember::update
// ==========================================================================
void listmember::update (const value &upd)
{
	string txml = upd.toxml ();
	::printf ("--upd--\n%s\n", txml.str());

	dbengine &DB = list.ldb.DB;
	dbtable &tlists = list.ldb.tlists;
	dbtable &tmembers = list.ldb.tmembers;
	
	load ();
	
	dbquery q (DB);
	q.update (tmembers).set (upd);
	q.where (tmembers["id"] == mdata["id"]);
	q.execvoid ();
	
	foreach (u, upd)
	{
		mdata[u.id()] = u;
	}
}

// ==========================================================================
// METHOD listmember::approved
// ==========================================================================
bool listmember::approved (void)
{
	load ();
	bool result = mdata["approved"].ival();
	return result;
}

void listmember::approved (bool param)
{
	value upd;
	upd["approved"] = param ? 1 : 0;
	
	update (upd);

}

// ==========================================================================
// METHOD listmember::needsprobe
// ==========================================================================
bool listmember::needsprobe (void)
{
	load ();
	bool result = mdata["needsprobe"].ival();
	return result;
}

void listmember::needsprobe (bool param)
{
	value upd;
	upd["needsprobe"] = param ? 1 : 0;
	
	update (upd);
}

// ==========================================================================
// METHOD listmember::moderator
// ==========================================================================
bool listmember::moderator (void)
{
	load ();
	bool res = mdata["moderator"].ival();
	return res;
}

void listmember::moderator (bool param)
{
	value upd;
	upd["moderator"] = param ? 1 : 0;
	update (upd);
}

// ==========================================================================
// METHOD listmember::password
// ==========================================================================
void listmember::password (const string &param)
{
	value upd;
	upd["password"] = kernel.pwcrypt.md5 (param);
	update (upd);
}

// ==========================================================================
// METHOD listmember::verifypassword
// ==========================================================================
bool listmember::verifypassword (const string &param)
{
	load ();
	string hash = mdata["password"];
	
	return kernel.pwcrypt.verify (param, hash);
}

// ==========================================================================
// CONSTRUCTOR mailinglist
// ==========================================================================
mailinglist::mailinglist (void)
	: ldb (*(new limadb("")))
{
	throw (defaultConstructorException());
}

mailinglist::mailinglist (limadb &wot, const statstring &pid)
	: ldb (wot), _id (pid)
{
	dbquery q (ldb.DB);
	value r;
	
	q.select (ldb.tlists);
	q.where (ldb.tlists["address"] == pid.sval());
	
	r = q.exec ();
	if (! r.count()) throw (mailinglistLoadFromDBException());
	dbid = r[0]["id"].sval();
}

mailinglist::mailinglist (mailinglist *orig)
	: ldb (orig->ldb), _id (orig->_id), loaded (orig->loaded),
	  dbid (orig->dbid)
{
	delete orig;
}

// ==========================================================================
// DESTRUCTOR mailinglist
// ==========================================================================
mailinglist::~mailinglist (void)
{
}

// ==========================================================================
// METHOD mailinglist::moderatedposts
// ==========================================================================
postrestriction_t mailinglist::moderatedposts (void)
{
	int result = (value) ldb.tlists.row(_id)["moderation_posts"];
	return (postrestriction_t) result;
}

void mailinglist::moderatedposts (postrestriction_t param)
{
	ldb.tlists.row(_id)["moderation_posts"] = (int) param;
	ldb.tlists.commitrows ();
}

// ==========================================================================
// METHOD mailinglist::moderatedsubs
// ==========================================================================
bool mailinglist::moderatedsubs (void)
{
	int result = (value) ldb.tlists.row(_id)["moderation_subscriptions"];
	return (result != 0);
}

void mailinglist::moderatedsubs (bool param)
{
	ldb.tlists.row(_id)["moderation_subscriptions"] = param ? 1 : 0;
	ldb.tlists.commitrows ();
}

// ==========================================================================
// METHOD mailinglist::replytoheader
// ==========================================================================
bool mailinglist::replytoheader (void)
{
	int result = (value) ldb.tlists.row(_id)["replyto"];
	return (result != 0);
}

void mailinglist::replytoheader (bool param)
{
	ldb.tlists.row(_id)["replyto"] = param ? 1 : 0;
	ldb.tlists.commitrows ();
}

// ==========================================================================
// METHOD mailinglist::subjecttag
// ==========================================================================
string *mailinglist::subjecttag (void)
{
	returnclass (string) res retain;
	res = (value) ldb.tlists.row(_id)["subjecttag"];
	
	return &res;
}

void mailinglist::subjecttag (const string &param)
{
	ldb.tlists.row(_id)["subjecttag"] = param;
	ldb.tlists.commitrows ();
}

// ==========================================================================
// METHOD mailinglist::name
// ==========================================================================
string *mailinglist::name (void)
{
	returnclass (string) res retain;
	res = (value) ldb.tlists.row(_id)["name"];
	return &res;
}

void mailinglist::name (const string &param)
{
	ldb.tlists.row(_id)["name"] = param;
	ldb.tlists.commitrows ();
}

// ==========================================================================
// METHOD mailinglist::owner
// ==========================================================================
string *mailinglist::owner (void)
{
	returnclass (string) res retain;
	res =  (value) ldb.tlists.row(_id)["owner"];
	return &res;
}

void mailinglist::owner (const string &param)
{
	ldb.tlists.row(_id)["owner"] = param;
	ldb.tlists.commitrows ();
}

// ==========================================================================
// METHOD mailinglist::salt
// ==========================================================================
string *mailinglist::salt (void)
{
	returnclass (string) res retain;
	res = (value) ldb.tlists.row(_id)["salt"];
	res = res.decode64 ();
	return &res;
}

// ==========================================================================
// METHOD mailinglist::memberexists
// ==========================================================================
bool mailinglist::memberexists (const statstring &address)
{
	if (members.exists (address)) return true;
	
	dbquery q (ldb.DB);
	q.select (ldb.tmembers);
	q.where ((ldb.tmembers["listid"] == dbid.sval().toint()) &&
			 (ldb.tmembers["address"] == address.sval()));
	
	value r = q.exec ();
	if (r.count ()) return true;
	return false;
}

// ==========================================================================
// METHOD mailinglist::listmoderators
// ==========================================================================
value *mailinglist::listmoderators (void)
{
	returnclass (value) res retain;
	
	dbquery q (ldb.DB);
	q.select (ldb.tmembers);
	q.where ((ldb.tmembers["listid"] == dbid.sval().toint()) &&
	         (ldb.tmembers["moderator"] == 1));
	q.indexby (ldb.tmembers["address"]);
	
	res = q.exec ();
	
	return &res;
}

// ==========================================================================
// METHOD mailinglist::listmembers
// ==========================================================================
value *mailinglist::listmembers (int offset, int amount)
{
	returnclass (value) res retain;
	
	dbquery q (ldb.DB);
	q.select (ldb.tmembers);
	q.where ((ldb.tmembers["listid"] == dbid.sval().toint()) &&
			 (ldb.tmembers["approved"] == 1));
	q.indexby (ldb.tmembers["address"]);
	
	if (amount) q.limit (offset, amount);
	
	res = q.exec ();
	return &res;
}

// ==========================================================================
// METHOD mailinglist::listmembermodq
// ==========================================================================
value *mailinglist::listmembermodq (void)
{
	returnclass (value) res retain;
	
	dbquery q (ldb.DB);
	q.select (ldb.tmembers);
	q.where ((ldb.tmembers["listid"] == dbid.sval().toint()) &&
			 (ldb.tmembers["approved"] == 0));
	q.indexby (ldb.tmembers["address"]);
	
	res = q.exec ();
	return &res;
}

// ==========================================================================
// METHOD mailinglist::approvemember
// ==========================================================================
void mailinglist::approvemember (const statstring &address)
{
	if (! memberexists (address)) return;
	listmember &m = getmember (address);
	m.approved (true);
}

// ==========================================================================
// METHOD mailinglist::disapprovemember
// ==========================================================================
void mailinglist::disapprovemember (const statstring &address)
{
	listmember &m = getmember (address);
	if (! m.approved())
	{
		dbquery q (ldb.DB);
		q.deletefrom (ldb.tmembers);
		q.where ((ldb.tmembers["address"] == address.sval()) &&
				 (ldb.tmembers["listid"] == dbid.sval().toint()));
				 
		q.execvoid ();
	}
}

// ==========================================================================
// METHOD mailinglist::deletemember
// ==========================================================================
void mailinglist::deletemember (const statstring &address)
{
	dbquery q (ldb.DB);
	q.deletefrom (ldb.tmembers);
	q.where ((ldb.tmembers["address"] == address.sval()) &&
			 (ldb.tmembers["listid"] == dbid.sval().toint()));
			 
	q.execvoid ();
}

// ==========================================================================
// METHOD mailinglist::createmember
// ==========================================================================
listmember &mailinglist::createmember (const statstring &address)
{
	value dat;
	dat["address"] = address;
	dat["moderator"] = 0;
	if (moderatedsubs()) dat["approved"] = 0;
	else dat["approved"] = 1;
	dat["password"] = "";
	dat["listid"] = dbid.sval().toint();
	
	if (members.exists (address))
		return members[address];

	dbquery q (ldb.DB);
	q.insertinto (ldb.tmembers).values (dat);
	q.execvoid ();

	return getmember (address);
}

// ==========================================================================
// METHOD mailinglist::getmember
// ==========================================================================
listmember &mailinglist::getmember (const statstring &address)
{
	if (! members.exists (address))
	{
		dbquery q (ldb.DB);
		q.select (ldb.tmembers["id"]);
		q.where ((ldb.tmembers["address"] == address.sval()) &&
				 (ldb.tmembers["listid"] == dbid.sval().toint()));
				 
		value v = q.exec ();
		
		if (! v.count())
		{
			throw (existentialMemberException());
		}
		
		members.set (address, new listmember (address, this));
	}
	
	return members[address];
}

// ==========================================================================
// METHOD mailinglist::dump
// ==========================================================================
value *mailinglist::dump (void)
{
	returnclass (value) res retain;
	
	dbquery q (ldb.DB);
	q.select (ldb.tmembers);
	q.where (ldb.tmembers["listid"] == dbid.sval().toint());
	res = q.exec ();
	return &res;
}
