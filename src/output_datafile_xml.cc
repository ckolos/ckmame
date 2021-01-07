/*
  output_datafile_xml.c -- write games to datafile.dtd XML files
  Copyright (C) 2011-2014 Dieter Baron and Thomas Klausner

  This file is part of ckmame, a program to check rom sets for MAME.
  The authors can be contacted at <ckmame@nih.at>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
  3. The name of the author may not be used to endorse or promote
     products derived from this software without specific prior
     written permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "OutputContextXml.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "output.h"
#include "util.h"

#define xml_string(X) (reinterpret_cast<const xmlChar *>(X))

OutputContextXml::OutputContextXml(const std::string &fname_, int flags) : fname(fname_) {
    if (fname.empty()) {
	f = stdout;
	fname = "*stdout*";
    }
    else {
	if ((f = fopen(fname.c_str(), "w")) == NULL) {
	    myerror(ERRDEF, "cannot create '%s': %s", fname.c_str(), strerror(errno));
            throw std::exception();
	}
    }

    doc = xmlNewDoc(xml_string("1.0"));
    doc->encoding = xml_string(strdup("UTF-8"));
    xmlCreateIntSubset(doc, xml_string("datafile"), xml_string("-//Logiqx//DTD ROM Management Datafile//EN"), xml_string("http://www.logiqx.com/Dats/datafile.dtd"));
    root = xmlNewNode(NULL, xml_string("datafile"));
    xmlDocSetRootElement(doc, root);
}

OutputContextXml::~OutputContextXml() {
    close();

    xmlFreeDoc(doc);
}

bool OutputContextXml::close() {
    auto ok = true;

    if (f != NULL) {
        if (xmlDocFormatDump(f, doc, 1) < 0) {
            ok = false;
        }
	if (f != stdout) {
            if (fclose(f) < 0) {
                ok = false;
            }
	}
    }
    
    f = NULL;

    return ok;
}

static void
set_attribute(xmlNodePtr node, const std::string &name, const std::string &value) {
    if (value.empty()) {
        return;
    }
    xmlSetProp(node, xml_string(name.c_str()), xml_string(value.c_str()));
}

static void
set_attribute_u64(xmlNodePtr node, const char *name, uint64_t value) {
    char b[128];
    snprintf(b, sizeof(b), "%" PRIu64, value);
    set_attribute(node, name, b);
}

static void
set_attribute_hash(xmlNodePtr node, const char *name, int type, Hashes *hashes) {
    set_attribute(node, name, hashes->to_string(type));
}


bool OutputContextXml::game(GamePtr game) {
    xmlNodePtr xmlGame = xmlNewChild(root, NULL, xml_string("game"), NULL);
    
    set_attribute(xmlGame, "name", game->name);
    set_attribute(xmlGame, "cloneof", game->cloneof[0]);
    /* description is actually required */
    xmlNewTextChild(xmlGame, NULL, xml_string("description"), xml_string(!game->description.empty() ? game->description.c_str() : game->name.c_str()));

    for (size_t i = 0; i < game->roms.size(); i++) {
        auto &rom = game->roms[i];
        xmlNodePtr xmlRom = xmlNewChild(xmlGame, NULL, xml_string("rom"), NULL);
        
        set_attribute(xmlRom, "name", rom.name);
        set_attribute_u64(xmlRom, "size", rom.size);
        set_attribute_hash(xmlRom, "crc", Hashes::TYPE_CRC, &rom.hashes);
        set_attribute_hash(xmlRom, "sha1", Hashes::TYPE_SHA1, &rom.hashes);
        set_attribute_hash(xmlRom, "md5", Hashes::TYPE_MD5, &rom.hashes);

        if (rom.where != FILE_INGAME) {
            set_attribute(xmlRom, "merge", rom.merge.empty() ? rom.name : rom.merge);
        }

        set_attribute(xmlRom, "status", status_name(rom.status));
    }

    for (size_t i = 0; i < game->disks.size(); i++) {
        auto d = &game->disks[i];
        xmlNodePtr disk = xmlNewChild(xmlGame, NULL, xml_string("disk"), NULL);

        set_attribute(disk, "name", disk_name(d));
        set_attribute_hash(disk, "sha1", Hashes::TYPE_SHA1, disk_hashes(d));
        set_attribute_hash(disk, "md5", Hashes::TYPE_MD5, disk_hashes(d));
        set_attribute(disk, "status", status_name(disk_status(d)));
    }
    
    return true;
}


bool OutputContextXml::header(DatEntry *dat) {
    xmlNodePtr header = xmlNewChild(root, NULL, xml_string("header"), NULL);
    
    xmlNewTextChild(header, NULL, xml_string("name"), xml_string(dat->name.c_str()));
    xmlNewTextChild(header, NULL, xml_string("description"), xml_string(dat->description.empty() ? dat->name.c_str() : dat->description.c_str()));
    xmlNewTextChild(header, NULL, xml_string("version"), xml_string(dat->version.c_str()));
    xmlNewTextChild(header, NULL, xml_string("author"), xml_string("automatically generated"));

    return true;
}
