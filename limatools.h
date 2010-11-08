// This file is part of OpenPanel - The Open Source Control Panel
// OpenPanel is free software: you can redistribute it and/or modify it 
// under the terms of the GNU General Public License as published by the Free 
// Software Foundation, using version 3 of the License.
//
// Please note that use of the OpenPanel trademark may be subject to additional 
// restrictions. For more information, please visit the Legal Information 
// section of the OpenPanel website on http://www.openpanel.com/

#ifndef _LIMATOOLS_H
#define _LIMATOOLS_H 1

#include <grace/str.h>
#include <grace/value.h>

#include "paths.h"

class limatools
{
public:
	/// Convert an encoded email-address to a real one.
	/// \param encoded The encoded string that will be decoded in place.
	static bool	toemail (string &encoded);
	
	/// Verify a complex hash.
	/// \param encodedaddress The address + hash
	/// \param salt The secret salt to use for verification
	static bool verifyhash (const string &encodedaddress,
							const string &salt);
	
	/// Create a hashed address.
	/// \param prefix The hash prefix (usually "listname+command")
	/// \param addr The address that should be encoded.
	/// \param salt The secret salt.
	static string *createhashed (const string &prefix, const string &addr,
								 const string &salt);
	
	/// Send en email to one or more recipients through sendmail.
	/// \param sender The 'mail from' address to use.
	/// \param rcpt Either an array of recipients or a single address.
	/// \param data The full mail message (headers + body)
	/// \param error On failure, an error message will be placed in here.
	/// \param xverp Instruct sendmail (postfix) to use VERP.
	static bool sendmail (const string &sender, const value &rcpt,
				          const string &data, string &error, bool xverp=false);
	
	/// Get a default reply message as configured either for the list or
	/// globally.
	/// \param listid The fully qualified list id (listname@domain).
	/// \param env Environment variables for the mail template.
	static string *getdefaultmsg (const string &listid, const string &type,
								  const value &env);
	
	/// Get an unparsed default reply message, either specifically for the
	/// list or a global template.
	static string *getdefaultmsg (const string &listid, const string &type);
	
	/// Update a default reply message specifically for a list.
	static bool setdefaultmsg (const string &listid, const string &type,
							   const string &data);
	
	/// Scan a queue directory for from/subject headers indexed by
	/// their on-disk message-id.
	static value *scandir (const string &path);
};

#endif
