// This file is part of OpenPanel - The Open Source Control Panel
// OpenPanel is free software: you can redistribute it and/or modify it 
// under the terms of the GNU General Public License as published by the Free 
// Software Foundation, using version 3 of the License.
//
// Please note that use of the OpenPanel trademark may be subject to additional 
// restrictions. For more information, please visit the Legal Information 
// section of the OpenPanel website on http://www.openpanel.com/

#include "limatools.h"
#include <grace/str.h>
#include <grace/strutil.h>
#include <grace/system.h>
#include <grace/filesystem.h>
#include <grace/process.h>
#include <grace/md5.h>

// ==========================================================================
// STATIC METHOD limatools::getdefaultmsg
// ==========================================================================
string *limatools::getdefaultmsg (const string &listname,
									  const string &type,
									  const value &env)
{
	returnclass (string) res retain;
	
	string fname = PATH_TMPL;
	if (fname[-1] != '/') fname.strcat ('/');
	fname.strcat (listname);
	fname.printf ("-%s.msg", type.str());
	
	if (fs.exists (fname)) res = fs.load (fname);
	else
	{
		fname = PATH_TMPL;
		if (fname[-1] != '/') fname.strcat ('/');
		fname.printf ("default-%s.msg", type.str());
		
		if (! fs.exists (fname))
		{
			res.printf ("Subject: lima mailinglist error\n\n");
			res.printf ("An error occured processing a default lima message "
						"for your\n");
			res.printf ("request of type '%s'. Please contact the list "
						"maintainer.\n", type.str());
		}
		else
		{
			res = fs.load (fname);
			res = strutil::valueparse (res, env);
		}
	}
	
	return &res;
}

string *limatools::getdefaultmsg (const string &listname, const string &type)
{
	returnclass (string) res retain;
	
	string fname = PATH_TMPL;
	if (fname[-1] != '/') fname.strcat ('/');
	fname.strcat (listname);
	fname.printf ("-%s.msg", type.str());
	if (! fs.exists (fname))
	{
		fname = PATH_TMPL;
		if (fname[-1] != '/') fname.strcat ('/');
		fname.printf ("default-%s.msg", type.str());
	}

	res = fs.load (fname);
	return &res;
}

// ==========================================================================
// STATIC METHOD limatools::setdefaultmsg
// ==========================================================================
bool limatools::setdefaultmsg (const string &listname, const string &type,
							   const string &contents)
{
	bool res;
	string fname = PATH_TMPL;
	if (fname[-1] != '/') fname.strcat ('/');
	fname.printf ("%s-%s.msg", listname.str(), type.str());
	res = fs.save (fname, contents);
	
	::printf ("* Save <%s>: %s\n", fname.str(), res ? "OK" : "FAIL");
	return res;
}

// ==========================================================================
// STATIC METHOD limatools::sendmail
// ==========================================================================
bool limatools::sendmail (const string &sender,
						  const value &xrcpt,
						  const string &data,
						  string &error,
						  bool xverp)
{
	value rcpt;
	if (xrcpt.count()) rcpt = xrcpt;
	else rcpt[xrcpt.sval()] = true;
	
	string buf;
	string cmdline = "/usr/lib/sendmail -bs";
	systemprocess proc (cmdline);
	proc.run ();
	
	try
	{
		buf = proc.gets();
		if (buf.toint() != 220)
		{
			proc.kill (SIGKILL);
			proc.serialize ();
			error.printf ("Submit error from=<%s>: %s",
						  sender.str(), buf.str());
			return false;
		}
		proc.puts ("HELO localhost\n");
		
		buf = proc.gets();
		if (buf.toint() != 250)
		{
			proc.kill (SIGKILL);
			proc.serialize ();
			error.printf ("Submit error from=<%s>: %s",
			    		  sender.str(), buf.str());
			return false;
		}
		
		proc.printf ("MAIL FROM: <%s>%s\n", sender.str(), xverp?" XVERP":"");
		buf = proc.gets();
		if (buf.toint() != 250)
		{
			proc.puts ("QUIT\n");
			proc.kill (SIGKILL);
			proc.serialize ();
			error.printf ("Submit error from=<%s>: %s",
						  sender.str(), buf.str());
			return false;
		}
		
		foreach (recipient, rcpt)
		{
			proc.printf ("RCPT TO: <%s>\n", recipient.name());
			buf = proc.gets();
			if (buf.toint() != 250)
			{
				proc.puts ("QUIT\n");
				proc.kill (SIGKILL);
				proc.serialize ();
				error.printf ("Submit error from=<%s> to=<%s>: %s",
							  sender.str(), recipient.str(), buf.str());
				return false;
			}
		}

		proc.puts ("DATA\n");
		buf = proc.gets();
		if (buf.toint() != 354)
		{
			proc.puts ("QUIT\n");
			proc.kill (SIGKILL);
			proc.serialize ();
			error.printf ("Submit error from=<%s>: %s",
						  sender.str(), buf.str());
			return false;
		}
		
		proc.puts (data);
		proc.puts ("\n.\n");
		buf = proc.gets();
		if (buf.toint() != 250)
		{
			proc.puts ("QUIT\n");
			proc.kill (SIGKILL);
			proc.serialize ();
			error.printf ("Submit error from=<%s>: %s",
						  sender.str(), buf.str());
			return false;
		}
		
		proc.puts ("QUIT\n");
		buf = proc.gets ();
	}
	catch (...)
	{
		proc.serialize ();
		return false;
	}
	
	proc.terminate ();
	proc.serialize ();
	return true;
}

// ==========================================================================
// STATIC METHOD limatools::createhashed
// ==========================================================================
string *limatools::createhashed (const string &prefix, const string &address,
								 const string &salt)
{
	returnclass (string) res retain;
	
	string hashme;
	string hashstr;
	string enc = strutil::regexp (address, "s/@/=/");
	res.printf ("%s+%s+%08x", prefix.str(), enc.str(), kernel.time.now());
	
	hashme = salt;
	hashme.strcat (res);
	
	md5checksum md5;
	md5.init ();
	md5.append (hashme);
	
	hashstr = md5.base64();
	hashstr = hashstr.left (22);
	
	res.printf ("+%s", hashstr.str());
	
	return &res;
}

// ==========================================================================
// STATIC METHOD limatools::verifyhash
// ==========================================================================
bool limatools::verifyhash (const string &encodedaddress,
							const string &salt)
{
	int elen = encodedaddress.strlen();
	if (elen < 34) return false;
	
	string b64hash = encodedaddress.right (22);
	
	// Strip the "+" and the base64 hash (sized 24)
	string nonhashpart = encodedaddress.left (elen-23);
	
	string salted = salt;
	salted.strcat (nonhashpart);
	
	md5checksum md5;
	md5.init ();
	md5.append (salted);
	
	string calchash = md5.base64 ();
	calchash = calchash.left (22);
	
	if (calchash != b64hash)
	{
		return false;
	}
	
	return true;
}

// ==========================================================================
// STATIC METHOD limatools::toemail
// ==========================================================================
bool limatools::toemail (string &encoded)
{
	int n, nn;
	n = nn = 0;
	
	while ((nn = encoded.strchr ('=', nn+1)) >= 0) n = nn;
	if (n<1) return false;
	encoded[n] = '@';
	return true;
}

// ==========================================================================
// STATIC METHOD limatools::scandir
// ==========================================================================
value *limatools::scandir (const string &path)
{
	returnclass (value) res retain;
	
	value dir = fs.ls (path);
	foreach (node, dir)
	{
		if (node.id().sval().strstr (".xml") > 0)
		{
			string msgid = node.id();
			msgid.cropat (".xml");
			value hdr;
			hdr.loadxml (node["path"]);
			res[msgid]["from"] = hdr["From"];
			res[msgid]["subject"] = hdr["Subject"];
		}
	}
	
	return &res;
}

