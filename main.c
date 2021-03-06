#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include "gui.h"

#ifndef PROGRAM_REVISION
#define PROGRAM_REVISION "#unknown"
#endif

//#define DEBUG_ENABLED DEBUG_ENABLED

#define PROG_VCOM 0
#define PROG_VSIM 1
#define PROG_VLIB 2
#define PROG_VMAP 3
#define PROG_VDEL 4
#define PROG_UNKNOWN 255

#define K *1024
#define M K K
#define G M K

#define ISOPT(cmd)  (strcmp(argv[i], cmd) == 0)
#define GETOPT(cmd) (strcmp(argv[i], cmd) == 0) && (++i < argc)

const char *getAfter (const char *__haystack, const char *__needle);
int get_application(const char *call);
int vsim(int argc, char **argv);
int vcom(int argc, char **argv);
int run_ghdl(char *command, ...);
int run_simulation(char *command, ...);
int run_gtkwave(char *toplevel, char *command, ...);
char* append_string(char **dest, const char *src);
pid_t system2(const char * command, int * infp, int * outfp);
void debug( const char* format, ... );

void debug( const char* format, ... ) {
#ifdef DEBUG_ENABLED
	va_list args;
	// fprintf( stderr, "[D] " );
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
	// fprintf( stderr, "\n" );
#endif
}

// Thanks GreenScape
// http://stackoverflow.com/questions/22802902/how-to-get-pid-of-process-executed-with-system-command-in-c
pid_t system2(const char * command, int * infp, int * outfp)
{
	int p_stdin[2];
	int p_stdout[2];
	pid_t pid;

	if (pipe(p_stdin) == -1)
		return -1;

	if (pipe(p_stdout) == -1) {
		close(p_stdin[0]);
		close(p_stdin[1]);
		return -1;
	}

	pid = fork();

	if (pid < 0) {
		close(p_stdin[0]);
		close(p_stdin[1]);
		close(p_stdout[0]);
		close(p_stdout[1]);
		return pid;
	} else if (pid == 0) {
		close(p_stdin[1]);
		dup2(p_stdin[0], 0);
		close(p_stdout[0]);
		dup2(p_stdout[1], 1);
		dup2(open("/dev/null", O_RDONLY), 2);
		/// Close all other descriptors for the safety sake.
		for (int i = 3; i < 4096; ++i)
			close(i);

		setsid();
		execl("/bin/sh", "sh", "-c", command, NULL);
		_exit(1);
	}
}

int run_ghdl(char *command, ...) {
	FILE *proc;
	char buf[1 K];
	char cmd[1 K];

	char *arr[5];
	char *start;
	char *ptr;
	int arrc;
	int i;

	va_list argptr;
	va_start(argptr, command);
	vsprintf(cmd, command, argptr);
	va_end(argptr);

	debug("RUN_GHDL: %s\n", cmd);
	proc = popen(cmd, "r");

	if (proc == NULL) {
		printf("Error: Could not invoke GHDL/GtkWave.\n");
		return 1;
	}

	// ../blink/src/top.vhd:32:19: no declaration for "counter_i2"
	// ../blink/src/abc.vhd:12:14:warning: package "mypack" does not require a body
	//                            v
	// ** Error: /tmp/filename.vhd(32): (vcom-1136) Unknown identifier "counter_i2".

	while(42){
		ptr = buf - 1;

		do {
			ptr++;
			*ptr = fgetc(proc);
		} while (*ptr != '\0' && *ptr != '\n' && *ptr != -1 && ptr < buf + sizeof(buf));
		if (*ptr == -1)
			break;
		*ptr = '\0';

		ptr = buf;
		start = buf;
		arrc = 0;

		//printf("** BUF: %s\n", buf);
		do { // Split into params
			if (*ptr == ' ') {
				arr[arrc++] = start;
				break;
			}
			if (arrc < 5 && (*ptr == ':' || *ptr == '\0')) {
				arr[arrc++] = start;
				if (*ptr == '\0')
					break;
				else
					*ptr++ = 0;
				start = ptr;
			}
		} while (*ptr++ != '\0');

		if (arrc == 4) {
			printf("** Error: %s(%s): (ghdl) %s\n", arr[0], arr[1], arr[3]);
		}
		else if (arrc == 5) {
			printf("** Warning: %s(%s): (ghdl) %s\n", arr[0], arr[1], arr[4]);
		}
		else {
			printf("** ghdl: ");
			for (i = 0; i < arrc; i++) {
				printf("%s", arr[i]);
			}
			printf("\n");
		}
		fflush(stdout);
	}

	return pclose(proc);
}

int run_simulation(char *command, ...) {
	FILE *proc;
	char buf[1 K];
	char cmd[1 K];

	va_list argptr;
	va_start(argptr, command);
	vsprintf(cmd, command, argptr);
	va_end(argptr);

	debug("RUN_SIM: %s\n", cmd);
	proc = popen(cmd, "r");

	if (proc == NULL) {
		printf("[E] Could not start the simulation.\n");
		return 1;
	}

	while(fgets(buf, sizeof(buf), proc)!=NULL){
		printf("** sim: %s", buf);
	}
	printf("\n");

	return pclose(proc);
}

int run_gtkwave(char *toplevel, char *command, ...) {
	FILE *fp;
	pid_t pid;
	char cmd[1 K];
	char lockpath[1 K];
	va_list argptr;

	sprintf(lockpath, "/tmp/model-ghdl-gtkw-%s.lock", toplevel);

	fp = fopen(lockpath,"r");
	if (fp) {
		fgets(cmd, sizeof(cmd), fp); // lets (ab)use the cmd variable here
		pid = atoi(cmd);
		fclose(fp);

		if (kill(pid, 0)) { // Check if the process still lives
			pid = -1;
		}
		/*else {
	    printf("GtkWave is already running.\n");
	}*/
	}
	else {
		pid = -1;
	}

	if (pid < 0) {
		va_start(argptr, command);
		vsprintf(cmd, command, argptr);
		va_end(argptr);

		debug("RUN_GTKWAVE: %s\n", cmd);

		pid = system2(cmd, NULL, NULL);
		//debug("--> PID=%d\n", pid);

		// Prevent gtkw from starting again each time
		fp = fopen(lockpath,"w");
		if (fp) {
			fprintf(fp, "%d", pid);
			fclose(fp);
		}
		else {
			printf("[W] Could not create temp file %s! Ignoring...", lockpath);
		}
	}

	return 0;
}


int vsim(int argc, char **argv)
{
	int ret;
	char *text = NULL;
	char *work = NULL;
	char *toplevel = NULL;
	char *gtkwPrefix = NULL;

	int i;
	char *ptr = NULL;
	char *lastPtr;
	char workdir[1 K];
	char sourcedir[1 K];
	char *params = NULL;
	char *simtime = NULL;
	char *simExt = NULL;
	char *outFileType = NULL;
	char vhdlver[16] = "";
	int precompiled = 1;
	FILE *fp;

	append_string(&params,"");

	gui_init(&argc, &argv);

	if (!getcwd(sourcedir, sizeof(sourcedir))) { // Default compile dir is cwd
		sourcedir[0] = 0;
		printf("[W] Could not get cwd!\n");
	}

	fp = fopen("/tmp/model-ghdl-vsim","r");
	if (fp) {
		fgets(workdir, sizeof(workdir), fp); // lets (ab)use the workdir variable here
		append_string(&simtime, workdir);
		fclose(fp);
	}
	else {
		append_string(&simtime, "100ns");
	}

	fp = fopen("/tmp/model-ghdl-vcom","r");
	if (fp) {
		fgets(workdir, sizeof(workdir), fp); // (ab)use workdir variable as temp
		if (!strcmp(workdir,"nopre\n"))
			precompiled = 0;
		fgets(workdir, sizeof(workdir), fp);
		workdir[strlen(workdir)-1] = 0;
		fgets(vhdlver, sizeof(vhdlver), fp);
		fclose(fp);
	}
	else {
		printf("[E] Could not read temp file /tmp/model-ghdl-vcom! Aborting...");
	}

	printf ("[I] Emulating vsim.\n");
	// -gui work.toplevel(RTL)
	for (i=1; i < argc; ++i) {
		if (ptr == NULL && GETOPT("-gui")) { // only allow once
			append_string(&ptr, argv[i]);
			lastPtr = ptr;
			for (; *ptr != 0; ptr++) {
				if (*ptr >= 'A' && *ptr <= 'Z')
					*ptr = *ptr - ('A'-'a'); // convert to lower case

				if (*ptr == '.') {
					*ptr++ = 0;
					work = lastPtr;
					lastPtr = ptr;
				}
				else if (*ptr == '(') {
					*ptr++ = 0;
					toplevel = lastPtr;
					lastPtr = ptr;
				}
			}
			// free(ptr); DO NOT FREE, we still need it.
			// ptr = NULL;
		}
		else if (GETOPT("-type")) {
			append_string(&simExt, argv[i]);
		}
		else if (GETOPT("-ghdl")) {
			append_string(&params, " ");
			append_string(&params, argv[i]);
		}
		else if (GETOPT("-gtkwprefix")) {
			gtkwPrefix = argv[i];
		}
		else {

		}
	}

	if (simExt == NULL)
		append_string(&simExt, "ghw");

	if (!strcmp(simExt,"ghw")) {
		append_string(&outFileType, "wave");
	}
	else if (!strcmp(simExt,"vcd")) {
		append_string(&outFileType, "vcd");
	}
	else if (!strcmp(simExt,"fst")) {
		append_string(&outFileType, "fst");
	}
	else {
		fprintf(stderr, "[E] Unknown output file type!");
		showMessage(MESSAGE_ERROR, "Error! Unknown output file type.", NULL, NULL);
		return 127;
	}

	chdir(workdir);

	if (gtkwPrefix == NULL) {
		append_string(&gtkwPrefix, "");
	}

	printf("[I] Compiling...\n");
	if (run_ghdl("ghdl -%c %s --work=%s --workdir=\"%s\" %s %s", (precompiled ? 'e' : 'm'), vhdlver, work, workdir, params, toplevel)) {
		fprintf(stderr, "[E] Compilation failed!");
		showMessage(MESSAGE_ERROR, "Error! Compilation failed.", NULL, NULL);
	}
	else {
		if (ret = showMessage(MESSAGE_INPUT, "Enter the simulation time: ", simtime, &text)) {
			free(simtime);
			simtime = NULL;
			append_string(&simtime, text);
			fp = fopen("/tmp/model-ghdl-vsim","w");
			if (fp) {
				fprintf(fp, "%s", simtime);
				fclose(fp);
			}

			printf("[I] Simulating...\n");
			if (run_simulation("%s/%s --stop-time=%s --%s=%s.%s", workdir, toplevel, simtime, outFileType, toplevel, simExt)) {
				fprintf(stderr, "[E] Simulation failed!");
				showMessage(MESSAGE_ERROR, "Error! Simulation failed.", NULL, NULL);
			}
			else {
				if (run_gtkwave(toplevel, "gtkwave %s/%s.%s --save=\"%s/%s%s.gtkw\"", workdir, toplevel, simExt, sourcedir, gtkwPrefix, toplevel)) {
					fprintf(stderr, "[E] Could not open GtkWave!");
					showMessage(MESSAGE_ERROR, "Error! Could not open GtkWave!", NULL, NULL);
				}
				printf("[I] DONE.\n");
			}
		}
		return 0;
	}

	free(ptr); // Now we can free it

	return 255;
}

int vcom(int argc, char **argv)
{
	int i;
	char workdir[1 K];
	char *params = NULL;
	char *work = NULL;
	char *files = NULL;
	char vhdlver[16] = "";
	FILE *fp = NULL;
	int precompile = 1;

	printf ("[I] Emulating vcom.\n");

	if (!getcwd(workdir, sizeof(workdir))) { // Default compile dir is cwd
		fprintf(stderr, "[E] Could not get cwd!\n");
		return 1;
	}

	for (i=1; i < argc; ++i) {
		if (GETOPT("-work")) {
			work = argv[i];
		}
		else if (GETOPT("-workdir")) {
			strcpy(workdir, argv[i]);
		}
		else if (ISOPT("-87")) {
			strcpy(vhdlver, "--std=87");
		}
		else if (ISOPT("-93")) {
			strcpy(vhdlver, "--std=93");
		}
		else if (ISOPT("-93c")) {
			strcpy(vhdlver, "--std=93c");
		}
		else if (ISOPT("-2000")) {
			strcpy(vhdlver, "--std=00");
		}
		else if (ISOPT("-2002")) {
			strcpy(vhdlver, "--std=02");
		}
		else if (ISOPT("-2008")) {
			strcpy(vhdlver, "--std=08");
		}
		else if (ISOPT("-no-precompile")) {
			precompile = 0;
		}
		else if (GETOPT("-ghdl")) {
			append_string(&params, " ");
			append_string(&params, argv[i]);
		}
		else if (argv[i][0] != '-'){ // VHDL file
			append_string(&files, " ");
			append_string(&files, argv[i]);
		}
	}

	if (!params)
		append_string(&params, "");
	if (!work)
		append_string(&work, "work");

	if (!files) {
		fprintf(stderr, "[E] No input files specified.\n");
		return 2;
	}

	// Info for vsim later on
	fp = fopen("/tmp/model-ghdl-vcom","w");
	if (fp) {
		fprintf(fp, "%s\n%s\n%s", (precompile ? "pre" : "nopre"), workdir, vhdlver);
		fclose(fp);
	}
	else {
		printf("[W] Could not create temp file /tmp/model-ghdl-vcom! Ignoring...");
	}

	run_ghdl("ghdl -i --work=%s --workdir=%s %s %s %s 2>&1",
		 work, workdir, vhdlver, params, files);
	run_ghdl("ghdl -%c --work=%s --workdir=%s %s %s %s 2>&1",
		 (precompile ? 'a' : 's'), work, workdir, vhdlver, params, files);

	free(files);

	printf("[I] DONE.\n");
	return 0;
}


char* append_string(char **dest, const char *src) {
	if (*dest == NULL) {
		*dest = malloc(strlen(src) * sizeof(char));
		if (*dest == NULL)
			return NULL;
		*dest[0] = 0;
	}
	else {
		*dest = realloc(*dest, (strlen(*dest) + strlen(src) + 1) * sizeof(char));
	}

	strcat(*dest, src);
	return *dest;
}

int main(int argc, char **argv)
{
	printf ("model-ghdl revision %s, compiled on %s.\n", PROGRAM_REVISION, __DATE__);

	switch (get_application(argv[0])) {
	case PROG_VCOM:
		return vcom(argc, argv);
	case PROG_VSIM:
		return vsim(argc, argv);
	case PROG_VMAP:
	case PROG_VLIB:
	case PROG_VDEL:
		return 0;
	default:
		return 255;
	}
}

// Detects which function to call depending on the program name in argv[0]
int get_application(const char *call) {
	char *pos;
	pos = (char*) getAfter(call, "/");
	if (strcmp(pos, "vcom") == 0) {
		return PROG_VCOM;
	}
	else if (strcmp(pos, "vsim") == 0) {
		return PROG_VSIM;
	}
	else if (strcmp(pos, "vlib") == 0) {
		return PROG_VLIB;
	}
	else if (strcmp(pos, "vmap") == 0) {
		return PROG_VMAP;
	}
	else if (strcmp(pos, "vdel") == 0) {
		return PROG_VDEL;
	}
	else {
		fprintf(stderr, "[E] Program not recognized: %s\n", pos);
		return PROG_UNKNOWN;
	}
}

// Returns the string after the last occurence of __needle
const char *getAfter (const char *__haystack, const char *__needle) {
	char *pos, *realPos;
	char *haystack;
	haystack = (char*) __haystack;
	pos = (char*) __haystack;
	while (pos != NULL) {
		realPos = pos + 1;
		pos = strstr(haystack, __needle);
		if (haystack == __haystack && pos == NULL) // If no __needle is present at all...
			realPos = (char*) __haystack; // Return the entire string
		haystack = pos + 1;
	}
	return realPos;
}
