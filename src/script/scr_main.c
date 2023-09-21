/*
pragma
Copyright (C) 2023 BraXi.

Quake 2 Engine 'Id Tech 2'
Copyright (C) 1997-2001 Id Software, Inc.

See the attached GNU General Public License v2 for more details.
*/
// scr_main.c

/*
TODO:

	- fix quakeworld strings
*/

#include "../qcommon/qcommon.h"
#include "scriptvm.h"
#include "script_internals.h"
#include "../server/sv_game.h"

#define PROGS_CHECK_CRC 1

qcvm_t* ScriptVM; // qcvm currently in use
qcvm_t* scrvm_server = NULL;
qcvm_t* scrvm_client = NULL;

builtin_t* scr_builtins;
int scr_numBuiltins = 0;

// these indicate prog names and their crc checksums
static char* progsfile_server = "progs/server.dat";
const int progsfile_server_crc = 28003;
static char* progsfile_client = "progs/client.dat";
const int progsfile_client_crc = 0;

/*
============
CheckScriptVM
============
*/
inline CheckScriptVM()
{
#ifdef SCRIPTVM_PARANOID
	if (ScriptVM == NULL)
		Com_Error(ERR_FATAL, "Script VM is NULL in %s\n", __FUNCTION__);
#endif
}


static char* Scr_VMName(scrvmtype_t vm)
{
	if (vm == SCRVM_NONE)
		return "uninitialized";
	return (vm == SCRVM_SERVER ? "server" : "client");
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

	CheckScriptVM();
	for (i = 0; i < ScriptVM->progs->numGlobalDefs; i++)
	{
		def = &ScriptVM->globalDefs[i];
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
	ddef_t* def;
	int			i;

	CheckScriptVM();
	for (i = 0; i < ScriptVM->progs->numFieldDefs; i++)
	{
		def = &ScriptVM->fieldDefs[i];
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
	ddef_t* def;
	int			i;

	CheckScriptVM();
	for (i = 0; i < ScriptVM->progs->numFieldDefs; i++)
	{
		def = &ScriptVM->fieldDefs[i];
		if (!strcmp(ScrInternal_String(def->s_name), name))
		{
			return def;
		}
	}
	return NULL;
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

	CheckScriptVM();
	for (i = 0; i < ScriptVM->progs->numGlobalDefs; i++)
	{
		def = &ScriptVM->globalDefs[i];
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

	CheckScriptVM();
	for (i = 0; i < ScriptVM->progs->numFunctions; i++)
	{
		func = &ScriptVM->functions[i];
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
	CheckScriptVM();

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
ScrInternal_LoadProgs

Loads progs .dat file, sets edict size
===============
*/
void ScrInternal_LoadProgs(qcvm_t *vm, char* prName, int progsType)
{
	int		len, i;
	byte	*raw;

	if (!vm)
	{
		Com_Error(ERR_FATAL, "%s: called but the qcvm is NULL\n", __FUNCTION__);
		return;
	}

	if(vm->progs)
	{
		Com_Error(ERR_FATAL, "%s: tried to load second instance of %s script\n", __FUNCTION__, Scr_VMName(progsType));
		return;
	}

	// load file
	len = FS_LoadFile(prName, (void**)&raw);
	if (!len || len == -1)
	{
		Com_Error(ERR_FATAL, "%s: couldn't load \"%s\"\n", __FUNCTION__, prName);
		return;
	}
	vm->progs = (dprograms_t*)raw;
	ScriptVM = vm;

	// byte swap the header
	for (i = 0; i < sizeof(*vm->progs) / 4; i++)
		((int*)vm->progs)[i] = LittleLong(((int*)vm->progs)[i]);

	if (vm->progs->version != PROG_VERSION)
	{
		//Com_Printf( "%s: \"%s\" is wrong version %i (should be %i)\n", __FUNCTION__, prName, vm->progs->version, PROG_VERSION);
		Com_Error(ERR_FATAL, "%s: \"%s\" is wrong version %i (should be %i)\n", __FUNCTION__, prName, vm->progs->version, PROG_VERSION);
		return;
	}

	vm->crc = CRC_Block(vm->progs, len);
#if PROGS_CHECK_CRC == 1
	if (progsType == SCRVM_CLIENT && vm->progs->crc != progsfile_client_crc || progsType == SCRVM_SERVER && vm->progs->crc != progsfile_server_crc)
	{
		Com_Error(progsType == SCRVM_CLIENT ? ERR_FATAL : ERR_DROP, "\"%s\" has wrong defs crc=%i (recompile progs with up to date headers)\n", prName, vm->progs->crc);
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

	if (progsType == SCRVM_SERVER)
	{
		vm->entity_size = vm->progs->entityfields * 4 + sizeof(gentity_t) - sizeof(sv_entvars_t);
		vm->globals_struct = (sv_globalvars_t*)((byte*)vm->progs + vm->progs->ofs_globals);
	}
	else if (progsType == SCRVM_CLIENT)
	{
		// FIXME
		Com_Error(ERR_FATAL, "no client scripts\n"); 
//		vm->entity_size = vm->progs->entityfields * 4 + sizeof(centity_t) - sizeof(cl_entvars_t);
//		vm->globals_struct = (cl_globalvars_t*)((byte*)vm->progs + vm->progs->ofs_globals);
	}
	
	vm->globals = (float*)vm->globals_struct;

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

	Com_Printf("Loaded %s progs: \"%s\" (CRC %i, size %iK.)\n", Scr_VMName(vm->progsType), prName, vm->crc, len / 1024);

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
qboolean Scr_CreateScriptVM(scrvmtype_t progsType)
{
	qcvm_t* scrvm = NULL;
	char* name = NULL;

	if (progsType == SCRVM_SERVER && scrvm_server != NULL || progsType == SCRVM_CLIENT && scrvm_client != NULL)
	{
		Com_Error(ERR_FATAL, "Tried to create second instance of %s script VM\n", Scr_VMName(progsType));
		return false;
	}

	scrvm = Z_Malloc(sizeof(qcvm_t));
	if (!scrvm)
	{
		Com_Error(ERR_FATAL, "Couldn't allocate %s script VM\n", Scr_VMName(progsType));
		return false;
	}

	scrvm->progsType = progsType;
	switch (progsType)
	{
	case SCRVM_SERVER:
		name = progsfile_server;
		scrvm_server = scrvm;

		Cmd_AddCommand("edict", cmd_printedict_f);
		Cmd_AddCommand("edicts", cmd_printedicts_f);
		break;

	case SCRVM_CLIENT:
		name = progsfile_client;
		scrvm_client = scrvm;
		break;

	default:
		Com_Error(ERR_FATAL, "Scr_CreateScriptVM: unknown VM type %i\n", progsType);
		break;
	};

	ScriptVM = scrvm;
	ScrInternal_LoadProgs(scrvm, name, progsType);
	ScriptVM = NULL; 

	return true;
}

/*
===============
Scr_DestroyScriptVM

Destroy QC virtual machine
===============
*/
void Scr_DestroyScriptVM(scrvmtype_t vmtype)
{
	qcvm_t *vm;
	
	if (vmtype == SCRVM_SERVER)
		vm = scrvm_server;
	else if (vmtype == SCRVM_CLIENT)
		vm = scrvm_client;
	else
		vm = NULL;

	if (!vm)
	{
		Com_Printf("Scr_DestroyScriptVM: vm (%s) is NULL\n", Scr_VMName(vmtype));
		return;
	}

	if (vm->progs)
		Z_Free(vm->progs);
	Z_Free(vm);

	if (scrvm_server && vmtype == SCRVM_SERVER)
	{
		Cmd_RemoveCommand("edict");
		Cmd_RemoveCommand("edicts");
		scrvm_server = NULL;
	}
	else if (scrvm_client && vmtype == SCRVM_CLIENT)
	{
		scrvm_server = NULL;
	}

	Scr_BindVM(SCRVM_NONE);
	Com_Printf("Destroyed %s script VM...\n", Scr_VMName(vmtype));
}


void Cmd_Script_PrintFunctions(void)
{
	int i;
	qcvm_t* qcvm = ScriptVM;

	if (!ScriptVM)
		return;

	for (i = scr_numBuiltins; i < qcvm->progs->numFunctions; i++) // unsafe start
	{
		char* srcFile = ScrInternal_String(qcvm->functions[i].s_file);
		char* funcName = ScrInternal_String(qcvm->functions[i].s_name);
		int numParms = (qcvm->functions[i].numparms);
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
qboolean Scr_BindVM(scrvmtype_t vmtype)
{
	if(scrvm_server && vmtype == SCRVM_SERVER)
	{
		ScriptVM = scrvm_server;
		return true;
	}
	else if (scrvm_client && vmtype == SCRVM_CLIENT)
	{
		ScriptVM = scrvm_client;
		return true;
	}
	else if (vmtype == SCRVM_NONE)
	{
		ScriptVM = NULL;
		return false;
	}
	Com_DPrintf(DP_SCRIPT, "Scr_BindVM failed with vmtype %i\n", vmtype);
	return false;
}


sv_globalvars_t* Scr_GetGlobals()
{
	CheckScriptVM();
	return ScriptVM->globals_struct;
}

int Scr_GetEntitySize()
{
	CheckScriptVM();
	return ScriptVM->entity_size;
}

int Scr_GetEntityFieldsSize()
{
	CheckScriptVM();
	return (ScriptVM->progs->entityfields * 4);
}


void Scr_Init()
{
//	Scr_Shutdown();
	ScriptVM = NULL;
	scrvm_server = scrvm_client = NULL;
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

	Scr_DestroyScriptVM(SCRVM_SERVER);
	Scr_DestroyScriptVM(SCRVM_CLIENT);

	scrvm_server = scrvm_client = NULL;
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
			for (i = 0; i < ScriptVM->progs->numFunctions; i++)
			{
				f = &ScriptVM->functions[i];
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


