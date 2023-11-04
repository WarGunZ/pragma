/*
pragma
Copyright (C) 2023 BraXi.

Quake 2 Engine 'Id Tech 2'
Copyright (C) 1997-2001 Id Software, Inc.

See the attached GNU General Public License v2 for more details.
*/
// scr_main.c

#define PROGS_CHECK_CRC 0

#include "../qcommon/qcommon.h"
#include "script_internals.h"
//#include "../server/sv_game.h"

qcvm_t* qcvm[NUM_SCRIPT_VMS];
qcvm_t* active_qcvm; // qcvm currently in use

builtin_t* scr_builtins;
int scr_numBuiltins = 0;

const qcvmdef_t vmDefs[NUM_SCRIPT_VMS] =
{
	{SCRVM_NONE, NULL, 0, "none"},
	{SCRVM_SERVER, "progs/server.dat", 32763, "game"},
	{SCRVM_CLIENT, "progs/client.dat", 0, "cgame"},
	{SCRVM_MENU, "progs/menus.dat", 0, "gui"}
};

#define TAG_LEVEL 778
#define	G_INT(o)	(*(int *)&active_qcvm->globals[o])


static const char* Scr_VMName(scrvmtype_t vm)
{
	return vmDefs[vm].name;
}

/*
============
ScrInternal_GlobalAtOfs
============
*/
ddef_t* ScrInternal_GlobalAtOfs(int ofs)
{
	ddef_t* def;
	int			i;

	CheckScriptVM(__FUNCTION__);
	for (i = 0; i < active_qcvm->progs->numGlobalDefs; i++)
	{
		def = &active_qcvm->globalDefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
ddef_t* ScrInternal_FieldAtOfs(int ofs)
{
	ddef_t	*def;
	int		i;

	CheckScriptVM(__FUNCTION__);
	for (i = 0; i < active_qcvm->progs->numFieldDefs; i++)
	{
		def = &active_qcvm->fieldDefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
Scr_FindEdictField
============
*/
ddef_t* Scr_FindEntityField(char* name)
{
	ddef_t	*def;
	int		i;

	CheckScriptVM(__FUNCTION__);
	for (i = 0; i < active_qcvm->progs->numFieldDefs; i++)
	{
		def = &active_qcvm->fieldDefs[i];
		if (!strcmp(ScrInternal_String(def->s_name), name))
		{
			return def;
		}
	}
	return NULL;
}

/*
=============
Scr_NewString
=============
*/
char* Scr_NewString(char* string)
{
	char* newb, * new_p;
	int		i, l;

	l = strlen(string) + 1;

	newb = Z_TagMalloc(l, TAG_LEVEL);

	new_p = newb;

	for (i = 0; i < l; i++)
	{
		if (string[i] == '\\' && i < l - 1)
		{
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}

	return newb;
}

/*
=============
Scr_ParseEpair

Can parse either fields or globals
returns false if error
=============
*/
qboolean Scr_ParseEpair(void* base, ddef_t* key, char* s)
{
	int		i;
	char	string[128];
	ddef_t* def;
	char* v, * w;
	void* d;
	scr_func_t func;

	d = (void*)((int*)base + key->ofs);

	switch (key->type & ~DEF_SAVEGLOBAL)
	{
	case ev_string:
		*(scr_string_t*)d = Scr_NewString(s) - active_qcvm->strings;
		break;

	case ev_float:
		*(float*)d = atof(s);
		break;

	case ev_vector:
		strcpy(string, s);
		v = string;
		w = string;
		for (i = 0; i < 3; i++)
		{
			while (*v && *v != ' ')
				v++;
			*v = 0;
			((float*)d)[i] = atof(w);
			w = v = v + 1;
		}
		break;

	case ev_entity:
		*(int*)d = ENT_TO_PROG(ENT_FOR_NUM(atoi(s)));
		break;

	case ev_field:
		def = Scr_FindEntityField(s);
		if (!def)
		{
			//			if (strncmp(s, "sky", 3) && strcmp(s, "fog"))
			//				Com_DPrintf(DP_ALL, "Can't find field %s\n", s);
			return false;
		}
		*(int*)d = G_INT(def->ofs);
		break;

	case ev_function:
		func = Scr_FindFunction(s);
		if (func == -1)
		{
			Com_Error(ERR_FATAL, "Can't find function %s\n", s);
			return false;
		}
		*(scr_func_t*)d = func;
		break;

	default:
		break;
	}
	return true;
}

/*
============
Scr_FindGlobal
============
*/
ddef_t* Scr_FindGlobal(char* name)
{
	ddef_t* def;
	int			i;

	CheckScriptVM(__FUNCTION__);
	for (i = 0; i < active_qcvm->progs->numGlobalDefs; i++)
	{
		def = &active_qcvm->globalDefs[i];
		if (!strcmp(ScrInternal_String(def->s_name), name))
			return def;
	}
	return NULL;
}


/*
============
Scr_FindFunction
============
*/
dfunction_t* ScrInternal_FindFunction(char* name)
{
	dfunction_t* func;
	int				i;

	CheckScriptVM(__FUNCTION__);
	for (i = 0; i < active_qcvm->progs->numFunctions; i++)
	{
		func = &active_qcvm->functions[i];
		if (!strcmp(ScrInternal_String(func->s_name), name))
			return func;
	}
	return NULL;
}


/*
===============
Scr_GenerateBuiltinsDefs

Writes builtins QC header file to disk
===============
*/
void Scr_GenerateBuiltinsDefs(char* outfile)
{
	int i, sv, cl, both, dev;
	char* str1, * str2;
	CheckScriptVM(__FUNCTION__);

	printf("// this file was generated by pragma engine version %s (build: %s)\n// DO NOT EDIT\n\n", PRAGMA_VERSION, PRAGMA_TIMESTAMP);

	for (i = 1, sv = cl = both = dev = 0; i != scr_numBuiltins; i++)
	{
		if (scr_builtins[i].devmode)
			dev++;
		switch (scr_builtins[i].execon)
		{
		case PF_BOTH:
			both++;
			break;
		case PF_SV:
			sv++;
			break;
		case PF_CL:
			cl++;
			break;
		}
	}

	printf("// number of builtins: %i\n// server: %i\n// client: %i\n// common: %i\n// %i developer builtins execute only when 'developer_script' is enabled\n\n", scr_numBuiltins, sv, cl, both, dev);

	for (i = 1; i != scr_numBuiltins; i++)
	{
		switch (scr_builtins[i].execon)
		{
		case PF_BOTH:
			str1 = "// both";
			break;
		case PF_SV:
			str1 = "// sv";
			break;
		case PF_CL:
			str1 = "// cl";
			break;
		default:
			str1 = "";
		}
		if (scr_builtins[i].devmode)
			str2 = "- devmode";
		else
			str2 = "";

		printf("%s = #%i; %s %s\n", scr_builtins[i].qcstring, i, str1, str2);
	}
}


/*
===============
Scr_LoadProgs

Loads progs .dat file, sets edict size
===============
*/
void Scr_LoadProgs(qcvm_t *vm, const char* filename)
{
	int		len, i;
	byte	*raw;

	if (!vm)
		Com_Error(ERR_FATAL, "%s: called but the active_qcvm is NULL\n", __FUNCTION__);

	if(vm->progs)
		Com_Error(ERR_FATAL, "%s: tried to load second instance of %s script\n", __FUNCTION__, Scr_VMName(vm->progsType));

	// load file
	len = FS_LoadFile(filename, (void**)&raw);
	if (!len || len == -1)
	{
		Com_Error(ERR_FATAL, "%s: couldn't load \"%s\"\n", __FUNCTION__, filename);
		return;
	}

	vm->offsetToEntVars = 56;
	vm->progsSize = len;
	vm->progs = (dprograms_t*)raw;
	active_qcvm = vm; // just in case..

	// byte swap the header
	for (i = 0; i < sizeof(*vm->progs) / 4; i++)
		((int*)vm->progs)[i] = LittleLong(((int*)vm->progs)[i]);

	if (vm->progs->version != PROG_VERSION)
	{
		if (vm->progs->version == 7 /*FTEQC*/)
			Com_Printf("%s: \"%s\" is FTE version and not all opcodes are supported in pragma\n", __FUNCTION__, filename);
		else
			Com_Error(ERR_FATAL, "%s: \"%s\" is wrong version %i (should be %i)\n", __FUNCTION__, filename, vm->progs->version, PROG_VERSION);
	}

	vm->crc = CRC_Block(vm->progs, len);

#if PROGS_CHECK_CRC == 1
	if (vm->progs->crc != vmDefs[vm->progsType].defs_crc_checksum )
	{
		Com_Error(ERR_DROP, "\"%s\" has wrong defs crc = '%i' (recompile progs with up to date headers)\n", filename, vm->progs->crc);
		return;
	}
#else
	Com_Printf("PROGS CRC CHECK DISABLED, CRC is %i\n", vm->crc);
#endif

	// cast the data from progs
	vm->functions = (dfunction_t*)((byte*)vm->progs + vm->progs->ofs_functions);
	vm->strings = (char*)vm->progs + vm->progs->ofs_strings;
	vm->globalDefs = (ddef_t*)((byte*)vm->progs + vm->progs->ofs_globaldefs);
	vm->fieldDefs = (ddef_t*)((byte*)vm->progs + vm->progs->ofs_fielddefs);
	vm->statements = (dstatement_t*)((byte*)vm->progs + vm->progs->ofs_statements);

	vm->globals_struct =((byte*)vm->progs + vm->progs->ofs_globals);	
	vm->globals = (float*)vm->globals_struct;

	vm->entity_size = vm->entity_size + (vm->progs->entityfields * 4);

	// byte swap all the data
	for (i = 0; i < vm->progs->numStatements; i++)
	{
		vm->statements[i].op = LittleShort(vm->statements[i].op);
		vm->statements[i].a = LittleShort(vm->statements[i].a);
		vm->statements[i].b = LittleShort(vm->statements[i].b);
		vm->statements[i].c = LittleShort(vm->statements[i].c);
	}
	for (i = 0; i < vm->progs->numFunctions; i++)
	{
		vm->functions[i].first_statement = LittleLong(vm->functions[i].first_statement);
		vm->functions[i].parm_start = LittleLong(vm->functions[i].parm_start);
		vm->functions[i].s_name = LittleLong(vm->functions[i].s_name);
		vm->functions[i].s_file = LittleLong(vm->functions[i].s_file);
		vm->functions[i].numparms = LittleLong(vm->functions[i].numparms);
		vm->functions[i].locals = LittleLong(vm->functions[i].locals);
	}

	for (i = 0; i < vm->progs->numGlobalDefs; i++)
	{
		vm->globalDefs[i].type = LittleShort(vm->globalDefs[i].type);
		vm->globalDefs[i].ofs = LittleShort(vm->globalDefs[i].ofs);
		vm->globalDefs[i].s_name = LittleLong(vm->globalDefs[i].s_name);
	}

	for (i = 0; i < vm->progs->numFieldDefs; i++)
	{
		vm->fieldDefs[i].type = LittleShort(vm->fieldDefs[i].type);
		if (vm->fieldDefs[i].type & DEF_SAVEGLOBAL)
			Com_Error(ERR_FATAL, "pr_fielddefs[%i].type & DEF_SAVEGLOBAL\n", i);

		vm->fieldDefs[i].ofs = LittleShort(vm->fieldDefs[i].ofs);
		vm->fieldDefs[i].s_name = LittleLong(vm->fieldDefs[i].s_name);
	}

	for (i = 0; i < vm->progs->numGlobals; i++)
		((int*)vm->globals)[i] = LittleLong(((int*)vm->globals)[i]);

#ifdef _DEBUG
	Scr_GenerateBuiltinsDefs(NULL);
#endif
}


/*
===============
Scr_CreateScriptVM

Create script execution context
===============
*/
void cmd_printedict_f(void);
void cmd_printedicts_f(void);

void Scr_CreateScriptVM(scrvmtype_t vmType, unsigned int numEntities, size_t entitySize, size_t entvarOfs)
{
	Scr_FreeScriptVM(vmType);
//	if (qcvm[progsType] != NULL)
//		Com_Error(ERR_FATAL, "Tried to create second instance of %s script VM\n", Scr_VMName(progsType));

	qcvm[vmType] = Z_Malloc(sizeof(qcvm_t));
	if (qcvm == NULL)
		Com_Error(ERR_FATAL, "Couldn't allocate %s script VM\n", Scr_VMName(vmType));

	qcvm_t* vm = qcvm[vmType];
	vm = qcvm[vmType];
	vm->progsType = vmType;

	vm->num_entities = numEntities;
	vm->offsetToEntVars = entvarOfs;
	vm->entity_size = entitySize; // invaild now, will be set properly in loadprogs

	// load progs from file
	Scr_LoadProgs(vm, vmDefs[vmType].filename);

	// allocate entities
	vm->entities = (vm_entity_t*)Z_Malloc(vm->num_entities * vm->entity_size);
	if (vm->entities == NULL)
		Com_Error(ERR_FATAL, "Couldn't allocate entities for %s script VM\n", Scr_VMName(vmType));

	// add developer comands
	if (vmType == SCRVM_SERVER)
	{
		Cmd_AddCommand("edict", cmd_printedict_f);
		Cmd_AddCommand("edicts", cmd_printedicts_f);
	}

	// print statistics
	dprograms_t* progs = vm->progs;
	Com_Printf("-------------------------------------\n");
	Com_Printf("%s qcvm: '%s'\n", Scr_VMName(vm->progsType), vmDefs[vmType].filename);
	Com_Printf("          Functions: %i\n", progs->numFunctions);
	Com_Printf("         Statements: %i\n", progs->numStatements);
	Com_Printf("         GlobalDefs: %i\n", progs->numGlobalDefs);
	Com_Printf("            Globals: %i\n", progs->numGlobals);
	Com_Printf("      Entity fields: %i\n", progs->numFieldDefs);
	Com_Printf(" Allocated entities: %i, %i bytes\n", vm->num_entities, vm->num_entities * Scr_GetEntitySize());
	Com_Printf("        Entity size: %i bytes\n", Scr_GetEntitySize());
	Com_Printf("\n");
	Com_Printf("       CRC checksum: %i\n", vm->crc);
	Com_Printf("      Programs size: %i bytes (%iKb)\n", vm->progsSize, vm->progsSize / 1024);
	Com_Printf("-------------------------------------\n");
}

/*
===============
Scr_DestroyScriptVM

Destroy QC virtual machine
===============
*/
void Scr_FreeScriptVM(scrvmtype_t vmtype)
{
	qcvm_t *vm = qcvm[vmtype];

	if (!vm)
	{
		return;
	}

	if (vm->entities)
		Z_Free(vm->entities);

	if (vm->progs)
		Z_Free(vm->progs);

	Z_Free(vm);

	if (vm->progsType == SCRVM_SERVER)
	{
		Cmd_RemoveCommand("edict");
		Cmd_RemoveCommand("edicts");
	}
	qcvm[vmtype] = NULL;

	Scr_BindVM(SCRVM_NONE);
	Com_Printf("freed %s script vm...\n", Scr_VMName(vmtype));
}


void Cmd_Script_PrintFunctions(void)
{
	qcvm_t* vm = active_qcvm;
	if (!vm)
		return;

	for (int i = scr_numBuiltins; i < vm->progs->numFunctions; i++) // unsafe start
	{
		char* srcFile = ScrInternal_String(vm->functions[i].s_file);
		char* funcName = ScrInternal_String(vm->functions[i].s_name);
		int numParms = (vm->functions[i].numparms);
		if (numParms)
			printf("#%i %s:%s(#%i)\n", i, srcFile, funcName, numParms);
		else
			printf("#%i %s:%s()\n", i, srcFile, funcName);
	}
}

/*
===============
Scr_GenerateBuiltinsDefs

Returns true if programs can be executed
===============
*/
void Scr_BindVM(scrvmtype_t vmtype)
{
	if (qcvm[vmtype] == NULL)
		return;
	active_qcvm = qcvm[vmtype];
	CheckScriptVM(__FUNCTION__);
}


void* Scr_GetGlobals()
{
	CheckScriptVM(__FUNCTION__);
	return active_qcvm->globals_struct;
}

int Scr_GetEntitySize()
{
	CheckScriptVM(__FUNCTION__);
	return active_qcvm->entity_size;
}

void *Scr_GetEntityPtr()
{
	CheckScriptVM(__FUNCTION__);
	return active_qcvm->entities;
}

int Scr_GetEntityFieldsSize()
{
	CheckScriptVM(__FUNCTION__);
	return (active_qcvm->progs->entityfields * 4);
}


void Scr_Init()
{
//	Scr_Shutdown();
	active_qcvm = NULL;
	
	for (scrvmtype_t i = 0; i < NUM_SCRIPT_VMS; i++)
		qcvm[i] = NULL;

	scr_numBuiltins = 0;
	Scr_InitSharedBuiltins();
}

void Scr_Shutdown()
{
	Scr_BindVM(SCRVM_NONE);

	if (scr_builtins)
	{
		Z_Free(scr_builtins);
		Com_DPrintf(DP_SCRIPT, "Freed script vm builtins...\n");
	}

	for (scrvmtype_t i = 1; i < NUM_SCRIPT_VMS; i++)
	{
		if(qcvm[i])
			Scr_FreeScriptVM(i);
	}
}

typedef enum
{
	XALIGN_NONE = 0,
	XALIGN_LEFT = 0,
	XALIGN_RIGHT = 1,
	XALIGN_CENTER = 2
} UI_AlignX;
void UI_DrawString(int x, int y, UI_AlignX alignx, char* string);

void PR_Profile(int x, int y)
{
	dfunction_t* f, * best;
	int			max;
	int			num;
	int			i;
	static int nexttime = 0;

	static char str[10][64];

	if (Sys_Milliseconds() > nexttime + 100)
	{
		nexttime = Sys_Milliseconds();
		memset(&str, 0, sizeof(str));
		num = 0;
		do
		{
			max = 0;
			best = NULL;
			for (i = 0; i < active_qcvm->progs->numFunctions; i++)
			{
				f = &active_qcvm->functions[i];
				if (f->profile > max)
				{
					max = f->profile;
					best = f;
				}
			}
			if (best)
			{
				if (num < 10)
				{
					Com_sprintf(str[num], sizeof(str[num]), "%7i %s in %s", best->profile, ScrInternal_String(best->s_name), ScrInternal_String(best->s_file));
					Com_sprintf(str[num], sizeof(str[num]), "%7i:%s", best->profile, ScrInternal_String(best->s_name));
					//Com_Printf(str[num]);
					//UI_DrawString(x, y, XALIGN_RIGHT, str[num]);
				}

				num++;
				best->profile = 0;
			}
		} while (best);
	}

	for(i = 0; i < 10; i++)
		UI_DrawString(x, y+10*i, XALIGN_RIGHT, str[i]);
}


