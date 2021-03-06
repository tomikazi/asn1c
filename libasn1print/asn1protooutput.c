// Copyright 2020-present Open Networking Foundation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <asn1parser.h>

#include "asn1printproto.h"
#include "asn1protooutput.h"

static abuf all_output_;

typedef enum {
    PRINT_STDOUT,
    GLOBAL_BUFFER,
} print_method_e;
static print_method_e print_method_;

typedef enum {
    SNAKECASE_LOWER,
    SNAKECASE_UPPER,
} snake_case_e;

#define	INDENT(fmt, args...)    do {        \
        if(!(flags & APF_NOINDENT2)) {       \
            int tmp_i = level;              \
            while(tmp_i--) safe_printf("    ");  \
        }                                   \
        safe_printf(fmt, ##args);                \
    } while(0)


/* Check printf's error code, to be pedantic. */
static int safe_printf(const char *fmt, ...) {
    int ret = 0;
    va_list ap;
    va_start(ap, fmt);

    switch(print_method_) {
    case PRINT_STDOUT:
        ret = vprintf(fmt, ap);
        break;
    case GLOBAL_BUFFER:
        ret = abuf_vprintf(&all_output_, fmt, ap);
        break;
    }
    assert(ret >= 0);
    va_end(ap);

    return ret;
}

// Replace any upper case chars with lower
static void
toLowercase(char *mixedCase) {
	int i = 0;
	while(mixedCase[i]) {
		(mixedCase)[i] = tolower(mixedCase[i]);
		i++;
	}
}

// Create new string with in lower case. Caller must free
static char *
toLowercaseDup(char *mixedCase) {
	char *mixedCaseDup = strdup(mixedCase);
	toLowercase(mixedCaseDup);
	return mixedCaseDup;
}

// Create new string with in PascalCase. Caller must free
static char *
toPascalCaseDup(char *mixedCase) {
	char *pascalCaseDup = strdup(mixedCase);
	int i = 0;
	int removed = 0;
	int lastWasUpper = 0;
	while(mixedCase[i]) {
		if (mixedCase[i] == '-' || mixedCase[i] == '&' || mixedCase[i] == '_') {
			pascalCaseDup[i-removed] = toupper(mixedCase[i+1]);
			i++;
			removed++;
			lastWasUpper = 1;
		} else if (i == 0) {
			pascalCaseDup[i] = toupper(mixedCase[i]);
			lastWasUpper = 1;
		} else if (mixedCase[i] >= 'A' && mixedCase[i] <= 'Z' && lastWasUpper) {
			pascalCaseDup[i-removed] = tolower(mixedCase[i]);
		} else if (mixedCase[i] >= 'A' && mixedCase[i] <= 'Z'){
			pascalCaseDup[i-removed] = mixedCase[i];
			lastWasUpper = 1;
		} else {
			pascalCaseDup[i-removed] = mixedCase[i];
			lastWasUpper = 0;
		}
		i++;
	}
	pascalCaseDup[i-removed] = '\0';
	return pascalCaseDup;
}

// Create new string with in upper case. Caller must free
// Any uppercase letters after the first one must be prefixed with '_'
static char *
toSnakeCaseDup(const char *mixedCase, const snake_case_e tocase) {
	int i = 0;
	int added = 0;
	int lastChanged = 0;
	char *snakeCase = strdup(mixedCase);
	int origlen = strlen(mixedCase);
	while(mixedCase[i]) {
		if (i == 0 && mixedCase[i] == '&') {
			added = -1;
			lastChanged = 1;
		} else if ((tocase == SNAKECASE_LOWER && i > 0) && mixedCase[i] >= 'A' && mixedCase[i] <= 'Z' && lastChanged == 0) {
			snakeCase = (char *)realloc(snakeCase, origlen + added + 1);
			snakeCase[i+added] = '_';
			snakeCase[i+added+1] = tolower(mixedCase[i]);
			added++;
			lastChanged = 1;
		} else if (tocase == SNAKECASE_UPPER && mixedCase[i] >= 'a' && mixedCase[i] <= 'z') {
			snakeCase[i+added] = toupper(mixedCase[i]);
			lastChanged = 1;
		} else if (tocase == SNAKECASE_UPPER && i > 0 && mixedCase[i] >= 'A' && mixedCase[i] <= 'Z') {
			snakeCase = (char *)realloc(snakeCase, origlen + added + 1);
			snakeCase[i+added] = '_';
			snakeCase[i+added+1] = toupper(mixedCase[i]);
			added++;
			lastChanged = 1;
		} else if (tocase == SNAKECASE_LOWER && mixedCase[i] >= 'A' && mixedCase[i] <= 'Z') {
			snakeCase[i+added] = tolower(mixedCase[i]);
			lastChanged = 1;
		} else if (mixedCase[i] == '-' || mixedCase[i] == '.') {
			snakeCase[i+added] = '_';
			lastChanged = 1;
		} else {
			snakeCase[i+added] = mixedCase[i];
			lastChanged = 0;
		}
		i++;
	}
	snakeCase[i+added] = '\0';

	return snakeCase;
}

static int
startNotLcLetter(char *name) {
	if (name[0] < 'a' || name[0] > 'z') {
		return 1;
	}
	return 0;
}

static void
proto_print_comments(char *comments) {
	char *str1, *saveptr1, *token;
	int j;
	for (j = 1, str1 = comments; ; j++, str1 = NULL) {
	   token = strtok_r(str1, "\n", &saveptr1);
	   if (token == NULL)
		   break;
	   safe_printf("// %s\n", token);
   }
}

static void
proto_print_oid(asn1p_oid_t *oid) {
	int ac;

	safe_printf(" {");
	for(ac = 0; ac < oid->arcs_count; ac++) {
		const char *arcname = oid->arcs[ac].name;
		safe_printf(" ");

		if(arcname) {
			safe_printf(arcname);
			if(oid->arcs[ac].number >= 0) {
				safe_printf("(%s)", asn1p_itoa(oid->arcs[ac].number));
			}
		} else {
			safe_printf(asn1p_itoa(oid->arcs[ac].number));
		}
	}
	safe_printf(" }");
}

static void
print_entries(proto_msg_def_t **entry, size_t entries,
              enum asn1print_flags2 flags, int level, int andfree) {
    for (int i=0; i < (int)(entries); i++) {
        struct proto_msg_def_s *proto_msg_def  = entry[i];
        INDENT("");
        if (proto_msg_def->repeated > 0) {
            safe_printf("repeated ");
        }
        char *typePc;
        if (strstr(PROTOSCALARTYPES, proto_msg_def->type) != NULL) {
            typePc = strdup(proto_msg_def->type);
        } else {
            typePc = toPascalCaseDup(proto_msg_def->type);
        }
        char *nameLsc = toSnakeCaseDup(proto_msg_def->name, SNAKECASE_LOWER);
        safe_printf("%s %s = %d", typePc, nameLsc, i+1);
        free(typePc);
        free(nameLsc);
        if (strlen(proto_msg_def->rules) > 0) {
            safe_printf(" [(validate.v1.rules).%s]", proto_msg_def->rules);
        }
        if (strlen(proto_msg_def->comments) > 0) {
            safe_printf("; // %s\n", proto_msg_def->comments);
        } else {
            safe_printf(";\n");
        }
        if (andfree) {
            free(proto_msg_def);
            entry[i] = NULL;
        }
    }
}

static void
proto_print_single_oneof(struct proto_msg_oneof_s *proto_oneof,
         enum asn1print_flags2 flags, int level, int andfree) {
    INDENT("");
    if (strlen(proto_oneof->comments)) {
        proto_print_comments(proto_oneof->comments);
    }

    INDENT("");
    safe_printf("oneof %s {\n", toSnakeCaseDup(proto_oneof->name, SNAKECASE_LOWER));
    level++;
    print_entries(proto_oneof->entry, proto_oneof->entries, flags, level, andfree);
    level--;
    INDENT("}\n");
}

static void
proto_print_single_msg(proto_msg_t *proto_msg, enum asn1print_flags2 flags,
		int level, int andfree) {
	if (strlen(proto_msg->comments)) {
		proto_print_comments(proto_msg->comments);
	}

	char *namePc = toPascalCaseDup(proto_msg->name);
	safe_printf("message %s {\n", namePc);
	free(namePc);
	level++;
    print_entries(proto_msg->entry, proto_msg->entries, flags, level, andfree);
    for (int i=0; i < (int)(proto_msg->oneofs); i++) {
        struct proto_msg_oneof_s *proto_oneof  = proto_msg->oneof[i];
        proto_print_single_oneof(proto_oneof, flags, level, andfree);
    }
	level--;
	INDENT("};\n\n");
}

static void
proto_print_single_enum(proto_enum_t *proto_enum, enum asn1print_flags2 flags,
		int level, int andfree) {
	int index = 0;
	int hasEnumZero = 0;
	if (strlen(proto_enum->comments)) {
		proto_print_comments(proto_enum->comments);
	}
	char *enumName = toPascalCaseDup(proto_enum->name);
	safe_printf("enum %s {\n", enumName);
	free(enumName);
	level++;
	// If it does not have a zero enum option add one
	for (int i = 0; i < (int)(proto_enum->defs); i++) {
		proto_enum_def_t *proto_enum_def = proto_enum->def[i];
		if (proto_enum_def->index == 0) {
			hasEnumZero = 1;
			break;
		}
	}
	char *enumNameUc = toSnakeCaseDup(proto_enum->name, SNAKECASE_UPPER);
	if (hasEnumZero == 0) {
		INDENT("%s_UNDEFINED = 0; // auto generated\n", enumNameUc);
	}
	for (int i = 0; i < (int)(proto_enum->defs); i++) {
		proto_enum_def_t *proto_enum_def = proto_enum->def[i];
		char *defName = toSnakeCaseDup(proto_enum_def->name, SNAKECASE_UPPER);
		INDENT("%s_%s = %d;\n", enumNameUc, defName,
				proto_enum_def->index < 0 ? index++ : proto_enum_def->index);
		free(defName);
		if (andfree) {
			free(proto_enum_def);
			proto_enum->def[i] = NULL;
		}
	}
	level--;
	free(enumNameUc);
	safe_printf("};\n\n");
}

void proto_print_msg(proto_module_t *proto_module, enum asn1print_flags2 flags, int level, int andfree) {
	proto_print_comments(proto_module->comments);

	char *moduleNameLc = toLowercaseDup(proto_module->modulename);
	safe_printf("////////////////////// %s.proto //////////////////////\n", moduleNameLc);
	free(moduleNameLc);
	safe_printf("// Protobuf generated");
	if(strlen(proto_module->srcfilename) > 0 && strrchr(proto_module->srcfilename, '/') != NULL) {
		safe_printf(" from %s ", strrchr(proto_module->srcfilename, '/'));
	} else {
		safe_printf(" from /%s ", proto_module->srcfilename);
	}
	safe_printf("by asn1c-%s\n// ", VERSION);

	safe_printf(proto_module->modulename);
	if (proto_module->oid != NULL ) {
		proto_print_oid(proto_module->oid);
	}
	safe_printf("\n");

	safe_printf("\nsyntax = \"proto3\";\n\n");

	char *sourceFileLc = toSnakeCaseDup(proto_module->srcfilename, SNAKECASE_LOWER);
	char *srcNoRelPath = proto_remove_rel_path(sourceFileLc);
	if (startNotLcLetter(srcNoRelPath) == 0) {
		safe_printf("package %s.v1;\n\n", srcNoRelPath);
	} else {
		safe_printf("package pkg%s.v1;\n\n", srcNoRelPath);
	}

	for (int i = 0; i < (int)(proto_module->imports); i++) {
		proto_import_t **proto_import = proto_module->import;
		proto_import_t *proto_importn = proto_import[i];
		char* importName = toLowercaseDup(proto_importn->path);
		if (startNotLcLetter(srcNoRelPath) == 0) {
			safe_printf("import \"%s/v1/%s.proto\";", srcNoRelPath, importName);
		} else {
			safe_printf("import \"pkg%s/v1/%s.proto\";", srcNoRelPath, importName);
		}
		free(importName);
		if (proto_importn->oid != NULL) {
			safe_printf(" //");
			proto_print_oid(proto_importn->oid);
		}
		safe_printf("\n");
	}

	safe_printf("import \"validate/v1/validate.proto\";\n\n");

	for (int i = 0; i < (int)(proto_module->enums); i++) {
		proto_enum_t *proto_enum = proto_module->protoenum[i];
		proto_print_single_enum(proto_enum, flags, level, andfree);
		if (andfree) {
			free(proto_enum);
			proto_module->protoenum[i] = NULL;
		}
	}

	for (int i = 0; i < (int)(proto_module->messages); i++) {
		proto_msg_t *message = proto_module->message[i];
		proto_print_single_msg(message, flags, level, andfree);
		if (andfree) {
			free(message);
			proto_module->message[i] = NULL;
		}
	}

	if (andfree) {
		free(proto_module);
		proto_module = NULL;
	}
}
