#include "limasubmit.h"
#include "../limadb.h"
#include "../limatools.h"
#include <grace/strutil.h>
#include <grace/md5.h>
#include <grace/system.h>
#include <grace/filesystem.h>
#include <grace/process.h>
#include <syslog.h>

APPOBJECT(limasubmitApp);

// ==========================================================================
// METHOD limasubmitApp::main
// ==========================================================================
int limasubmitApp::main (void)
{
	// We need a recipient and sender argument
	if (argv["*"].count() < 2)
	{
		ferr.printf ("%% Missing required arguments\n");
		return 1;
	}
	
	// We'll be using regular syslog for this adventure.
	openlog ("lima-submit", 0, LOG_MAIL);
	
	string message; // storage for the submitted mail message
	string buffer; // block buffer for reading the message.
	value env; // environment variables for standard reply messages
	string subscriber; // storage for extracted subscriber
	string hashedaddr; // storage for an address hash.
	
	string recipient = argv["*"][0];
	string sender = argv["*"][1];
	
	if (sender.strncmp ("MAILER-DAEMON", 13) == 0)
	{
		syslog (LOG_ERR, "Skipping message from MAILER-DAEMON");
		return 0;
	}
	
	// Read the message.
	while (! fin.eof())
	{
		buffer = fin.read (1024);
		if (! buffer.strlen()) break;
		message.strcat (buffer);
	}
	
	limadb mydb (PATH_LIMADB);
	
	// Implications extracted from the recipient address.
	string userAndExtensionPart;
	string domainPart;
	string userPart;
	string extensionPart;
	string command;
	string encduser;
	string theaddress; // temp address storage
	
	domainPart = recipient;
	userAndExtensionPart = domainPart.cutat ('@');
	
	// userparts lacking a '+' here are messages sent to the actual
	// list address.
	if (userAndExtensionPart.strchr ('+') < 0)
	{
		userPart = userAndExtensionPart;
	}
	else
	{
		// so now we have an email-address in the form:
		// listname+{stuff}@domainname
		// so we'll first strip off the 'listname' part and keep
		// the rest for further processing inside extensionPart.
		extensionPart = userAndExtensionPart;
		userPart = extensionPart.cutat ('+');
	}
	
	// If the extensionpart has further elements, extract the 'command'-
	// part of this extension first.
	if (extensionPart.strchr ('+') >= 0)
	{
		command = extensionPart.cutat ('+');
		
		// we will use this in the case of confirms and probe-bounces.
		if (extensionPart.strlen() > 32)
		{
			encduser = extensionPart.left (extensionPart.strlen()-32);
		}
	}
	else
	{
		// Just purely a command, no further parameters encoded in the
		// address.
		command = extensionPart;
		extensionPart.crop ();
	}
	
	// Come up with the name, fully qualified id and owner-address
	// for the list.
	string listname = userPart;
	string listid = userPart;
	listid.printf ("@%s", domainPart.str());
	string listowner = userPart;
	listowner.printf ("+owner@%s", domainPart.str());
	
	// Make sure the list exists.
	if (! mydb.listexists (listid))
	{
		syslog (LOG_ERR, "Failure from=<%s> to=<%s>: List does not "
				"exist", sender.str(), listid.str());
		string msg = limatools::getdefaultmsg (listid, "nosuchlist", env);
		sendmail (listowner, sender, msg);
		return 0;
	}
	
	// Try to load the list.
	if (! mydb.loadlist (listid))
	{
		syslog (LOG_ERR, "Failure from=<%s> to=<%s>: Error loading "
				"dbfile for list", sender.str(), listid.str());
		
		string msg = limatools::getdefaultmsg (listid, "generror", env);
		sendmail (listid, sender, msg);
		return 0;
	}
	
	// Connect it to a list-object.
	mailinglist list (mydb, listid);
	string listsalt = list.salt();

	// Set some standard variables for bounce/reply messages.
	env["listid"] = listid;
	env["subjecttag"] = list.subjecttag();
	env["title"] = list.name();
	env["listname"] = listname;
	env["listowner"] = listowner;
	env["domainname"] = domainPart;
	
	// Is this a message straigt to the list?
	if (! command.strlen())
	{
		// Sender must be verified or post moderation must be set to
		// open posting.
		if (list.memberexists (sender) || (list.moderatedposts() == POST_OPEN))
		{
			syslog (LOG_INFO, "Submit from=<%s> to=<%s>",
					sender.str(), recipient.str());
		
			enqueue (message, recipient.str(), list.subjecttag(),
					 list.replytoheader());
		}
		else // NOWAI
		{
			syslog (LOG_ERR, "Non-member submit from=<%s> to=<%s>, bouncing",
					sender.str(), recipient.str());
					
			string msg = limatools::getdefaultmsg (listid, "notmember", env);
			sendmail (listowner, sender, msg);
		}
		
		return 0;
	}

	// Not a submit, so there's a command to handle.
	syslog (LOG_INFO, "Handling from=<%s> to=<%s> cmd=<%s>",
			sender.str(), listid.str(), command.str());
	
	caseselector (command)
	{
		incaseof ("bounce") :
			// This is just an XVERP bounce, so the encoded e-mail address
			// is the entire extension part.
			if (limatools::toemail (extensionPart))
			{
				// Find the member.
				if (list.memberexists (extensionPart))
				{
					// Member found, set the needsprobe flag.
					syslog (LOG_INFO, "Bounce received from=<%s> to=<%s>",
							sender.str(), listid.str());
					listmember member = list.getmember (extensionPart);
					member.needsprobe (true);
				}
				else
				{
					syslog (LOG_ERR, "Unknown bounce from=<%s> to=<%s> "
							"encoded_member=<%s>", sender.str(),
							listid.str(), extensionPart.str());
				}
			}
			else
			{
				syslog (LOG_ERR, "Erronous bounce from=<%s> to=<%s> ",
						sender.str(), listid.str());
				// erronous bounce, log and ignore.
			}
			break;
		
		incaseof ("probe") :
			// Uhoh, a bounce-probe got sent to us by the cold-hearted
			// mailserver, this means we've got dead meat in our
			// subscriberlist. That is, if this is a genuine hashed
			// bounce that passes the smell test.
			if (limatools::verifyhash (userAndExtensionPart, listsalt))
			{
				// We should already have separated all the hashing
				// from the encoded user part earlier on.
				if (encduser.strlen())
				{
					theaddress = encduser;
					limatools::toemail (theaddress);
					
					if (list.memberexists (theaddress))
					{
						syslog (LOG_INFO, "Probe bounce from=<%s> to=<%s> "
								"bounced_addr=<%s>", sender.str(),
								listid.str(), theaddress.str());
							
						list.deletemember (theaddress);
					}
					else
					{
						syslog (LOG_ERR, "Failed probe bounce from=<%s> to=<%s> "
								"bounced_addr=<%s>: Address unknown", sender.str(),
								listid.str(), theaddress.str());
							
					}
				}
				else
				{
					syslog (LOG_ERR, "Decoding error on bounce "
							"from=<%s> to=<%s> addr=<%s>",
							sender.str(), listid.str(),
							userAndExtensionPart.str());
				}
			}
			else
			{
				syslog (LOG_ERR, "Bounce with failed hash from=<%s> "
						"to=<%s> bounced_addr=<%s>", sender.str(),
						listid.str());
			}
			break;
		
		incaseof ("subscribe") :
			// This can either be an implicit subscribe for the
			// sender address, or an explicit one in the form
			// of a message to
			// listname+subscribe+user=domain.com@domain.tld
			if (extensionPart.strlen()) // explicit?
			{
				if (! limatools::toemail (extensionPart))
				{
					syslog (LOG_ERR, "Error handling explicit "
							"subscribe from=<%s> to=<%s> "
							"explicit_addr=<%s>", sender.str(),
							listid.str(), recipient.str());
					break;
				}
				
				subscriber = extensionPart;
			}
			else
			{
				subscriber = sender;
			}
			
			syslog (LOG_INFO, "Received subscribe request addr=<%s> "
					"list=<%s>", subscriber.str(), listid.str());

			env["subscriber"] = subscriber;

			// Created a hashed confirmation address the user can
			// reply to.
			string pfx;
			pfx.printf ("%s+sc", listname.str());
			
			hashedaddr = limatools::createhashed (pfx, subscriber, listsalt);
			hashedaddr.printf ("@%s", domainPart.str());
			env["replyaddr"] = hashedaddr;
			
			string msg = limatools::getdefaultmsg (listid, "confirmsub", env);
			sendmail (listowner, subscriber, msg);
			break;
		
		incaseof ("sc") :
			// Subscription confirmation with hashed goodies.
			if (limatools::verifyhash (userAndExtensionPart, listsalt))
			{
				if (encduser.strlen())
				{
					limatools::toemail (encduser);
					string theaddress = encduser;
					
					if (list.memberexists (theaddress))
					{
						syslog (LOG_WARNING, "Subscription confirmation "
								"received for an address that is "
								"already subscribed from=<%s> to=<%s> "
								"explicit_addr=<%s>", sender.str(),
								listid.str(), recipient.str());
					}
					else
					{
						list.createmember (theaddress);
						
						if (! list.memberexists (theaddress))
						{
							syslog (LOG_ERR, "Error creating member "
									"for list from=<%s> for=<%s> list=<%s>",
									sender.str(), theaddress.str(),
									listid.str());
						}
						else
						{
							syslog (LOG_INFO, "Confirmed subscription "
									"addr=<%s> list=<%s>", theaddress.str(),
									listid.str());
						}
						
						string msg = limatools::getdefaultmsg (listid, "welcome", env);
						sendmail (listowner, theaddress, msg);
					}
				}
			}
			else
			{
				syslog (LOG_ERR, "Subscription confirmation with "
						"failed hash from=<%s> to=<%s> explicit_addr=<%s>",
						sender.str(), listid.str(), recipient.str());
			}
			break;
		
		incaseof ("owner") :
			syslog (LOG_INFO, "Skipping message to list+owner");
			break;
		
		incaseof ("unsubscribe") :
			// verify account is subscribed.
			// create confirmation message.
			if (extensionPart.strlen()) // explicit?
			{
				if (! limatools::toemail (extensionPart))
				{
					syslog (LOG_ERR, "Error handling explicit "
							"unsubscribe from=<%s> to=<%s> "
							"explicit_addr=<%s>", sender.str(),
							listid.str(), recipient.str());
					break;
				}
				
				subscriber = extensionPart;
			}
			else
			{
				subscriber = sender;
			}
			
			if (! list.memberexists (subscriber))
			{
				syslog (LOG_ERR, "Error handling unsubscribe "
						"request from=<%s> to=<%s> addr=<%s>: "
						"Member not found", sender.str(),
						listid.str(), recipient.str());
				break;
			}

			syslog (LOG_INFO, "Received unsubscribe request addr=<%s> "
					"list=<%s>", subscriber.str(), listid.str());
					
			string pfx;
			pfx.printf ("%s+uc", listname.str());
			
			hashedaddr = limatools::createhashed (pfx, subscriber, listsalt);
			hashedaddr.printf ("@%s", domainPart.str());
			env["replyaddr"] = hashedaddr;
			
			string msg = limatools::getdefaultmsg (listid, "confirmunsub", env);
			sendmail (hashedaddr, subscriber, msg);
			break;
		
		incaseof ("uc") :
			// More hash validation at hand.
			if (limatools::verifyhash (userAndExtensionPart, listsalt))
			{
				if (encduser.strlen())
				{
					limatools::toemail (encduser);
					string theaddress = encduser;
					
					if (list.memberexists (theaddress))
					{
						// Ok, so we've got all corners covered, we can
						// safely remove the member.
						list.deletemember (theaddress);
						
						syslog (LOG_INFO, "Confirmed unsubscribe addr=<%s> "
								"list=<%s>", theaddress.str(), listid.str());
					}
					else
					{
						syslog (LOG_ERR, "Error handling unsubscription "
								"confirmation from=<%s> to=<%s> "
								"addr=<%s>: Member not found",
								sender.str(), listid.str(),
								recipient.str());
					}
				}
				else
				{
					syslog (LOG_ERR, "Error decoding member address "
							"from unsubscription confirmation "
							"from=<%s> to=<%s> addr=<%s>",
							sender.str(), listid.str(), recipient.str());
				}
			}
			else
			{
				syslog (LOG_ERR, "Invalid hash for unsubscription "
								 "confirmation from=<%s> to=<%s> "
								 "addr=<%s>", sender.str(), listid.str(),
								 recipient.str());
			}
			break;
		
		defaultcase :
			// generate error reply.
			break;
	}
	
	return 0;
}

// ==========================================================================
// METHOD limasubmitApp::sendmail
// ==========================================================================
bool limasubmitApp::sendmail (const string &sender,
							  const string &recipient,
							  const string &data)
{
	string err;
	bool res;
	res = limatools::sendmail (sender, recipient, data, err, false);
	if (! res)
	{
		syslog (LOG_ERR, "%s", err.str());
	}
	return res;
}

// ==========================================================================
// METHOD limasubmitApp::enqueue
// ==========================================================================
void limasubmitApp::enqueue (const string &msg, const string &listname,
							 const string &subjtag, bool replyto)
{
	string qdir  = PATH_INQUEUE;
	string nqdir;
	string cqdir;
	string msgdata =  msg;
	
	qdir.printf ("/%s", listname.str());
	if (! (fs.exists (qdir) || fs.mkdir (qdir)))
	{
		syslog (LOG_ERR, "Queue error finding %s", qdir.str());
		exit (1);
	}
	
	nqdir = qdir;
	nqdir.strcat ("/new");
	
	if (! (fs.exists (nqdir) || fs.mkdir (nqdir)))
	{
		syslog (LOG_ERR, "Queue error finding %s", nqdir.str());
		exit (1);
	}
	
	cqdir = qdir;
	cqdir.strcat ("/cur");
	
	if (! (fs.exists (cqdir) || fs.mkdir (cqdir)))
	{
		syslog (LOG_ERR, "Queue error finding %s", cqdir.str());
		exit (1);
	}
	
	string uuid = strutil::uuid();
	
	string nqpath = nqdir;
	string nqpathxml = nqdir;
	string cqpath = cqdir;
	string cqpathxml = cqdir;
	nqpath.printf ("/%s.msg", uuid.str());
	nqpathxml.printf ("/%s.xml", uuid.str());
	cqpath.printf ("/%s.msg", uuid.str());
	cqpathxml.printf ("/%s.xml", uuid.str());
	
	bool rewrote = false;
	value hdr;
	value mysplt = strutil::splitlines (msgdata);
	
	foreach (line, mysplt)
	{
		if (! line.sval().strlen()) break;
		
		value h = strutil::parsehdr (line.sval());
		if (h.exists ("Subject") && subjtag.strlen())
		{
			string rtag;
			rtag.printf ("[%s]", subjtag.str());
			
			string nsubj = h["Subject"];
			if (nsubj.strstr (rtag) < 0)
			{
				nsubj = rtag;
				nsubj.printf (" %s", h["Subject"].cval());
				string nhdr = "Subject: ";
				nhdr.strcat (nsubj);
				line = nhdr;
				rewrote = true;
			}
			
			h["Subject"] = nsubj;
		}
		hdr << h;
	}
	
	if (replyto)
	{
		string hdr;
		hdr.printf ("Reply-to: <%s>", listname.str());
		mysplt.insertval(1) = hdr;
	}
	
	string listid;
	listid.printf ("List-Id: <%s>", listname.str());
	mysplt.insertval(1) = listid;
	
	msgdata.crop ();
	foreach (line, mysplt)
	{
		msgdata.printf ("%s\n", line.cval());
	}
	
	if (! fs.save (nqpath, msgdata))
	{
		syslog (LOG_ERR, "Error queueing %s", nqpath.str());
		exit (1);
	}
	if (! hdr.savexml (nqpathxml))
	{
		syslog (LOG_ERR, "Error queueing %s", nqpathxml.str());
		exit (1);
	}
	if (! fs.mv (nqpath, cqpath))
	{
		syslog (LOG_ERR, "Error queueing %s", cqpath.str());
		exit (1);
	}
	if (! fs.mv (nqpathxml, cqpathxml))
	{
		syslog (LOG_ERR, "Error queueing %s", cqpathxml.str());
		exit (1);
	}
	
	// we're now succesfully queued.
}
