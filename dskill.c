#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>
#include <limits.h>
#include <wordexp.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>
#include <CoreServices/CoreServices.h>

#define EVIL ".DS_Store"
#define LATENCY 0.1
#define ID "xyz.space55.dskill"
#define PLIST_PATH "~/Library/LaunchAgents/"ID".plist"
#define NUM_EXCLUDES 16
#define NUM_PATHS 16

char *help_msg =
"Kill all .DS_Store!\n"
"\n"
"USAGE\n"
"\n"
"  $ dskill <cmd> [opts]\n"
"\n"
"COMMANDS\n"
"\n"
"  kill           remove all .DS_Store recursively under current directory\n"
"  guard          start a process to prevent creation of .DS_Store recursively\n"
"                 under current directory\n"
"  service        manage launchd service for dskill guard\n"
"\n"
"OPTIONS\n"
"\n"
"  -e, --exclude  exclude a directory pattern\n"
"  -h, --help     display help message\n";

char *service_help_msg =
"Manage dskill guard service\n"
"\n"
"USAGE\n"
"\n"
"  $ dskill service <cmd> -- [flags]\n"
"\n"
"COMMANDS\n"
"\n"
"  start       install and start the launchd service, run `dskill guard` in the\n"
"              background\n"
"  stop        stop the service\n"
"  install     install the service plist to ~/Library/LaunchAgents\n"
"  uninstall   stop and remove the service plist from ~/Library/LaunchAgents\n";

typedef struct flags {
	int num_excludes;
	char *excludes[NUM_EXCLUDES];
	int num_paths;
	char *paths[NUM_PATHS];
	bool help;
} flags;

flags flags_new() {
	return (flags) {
		.num_excludes = 0,
		.excludes = {},
		.num_paths = 0,
		.paths = {},
		.help = false,
	};
}

void flags_free(flags *f) {
	for (int i = 0; i < f->num_excludes; i++) {
		free(f->excludes[i]);
	}
	for (int i = 0; i < f->num_paths; i++) {
		free(f->paths[i]);
	}
	f->num_excludes = 0;
}

bool is_evil(char *path) {
	return strcmp(path + strlen(path) - strlen(EVIL), EVIL) == 0;
}

void error(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

void warn(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

void systemf(const char *fmt, ...) {
	char cmd[1024];
	va_list args;
	va_start(args, fmt);
	vsprintf(cmd, fmt, args);
	va_end(args);
	system(cmd);
}

char *expand(char *path) {
	wordexp_t exp_result;
	wordexp(path, &exp_result, 0);
	char *expanded = strdup(exp_result.we_wordv[0]);
	wordfree(&exp_result);
	return expanded;
}

bool is_excluded(char *path, flags *f) {
	char *path2 = strdup(path);
	char *dir = dirname(path2);
	for (int i = 0; i < f->num_excludes; i++) {
		char *pat = f->excludes[i];
		// TODO
		if (strcmp(dir, pat) == 0) {
			return true;
		}
	}
	free(path2);
	return false;
}

void clean_dir(char *dir, flags *f) {

	DIR *d = opendir(dir);
	struct dirent *entry;

	if (!d) {
		warn("failed to open directory \"%s\"\n", dir);
		return;
	}

	while ((entry = readdir(d))) {

		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		char path[PATH_MAX];

		if (strcmp(dir, ".") == 0) {
			sprintf(path, "%s", entry->d_name);
		} else {
			sprintf(path, "%s/%s", dir, entry->d_name);
		}

		if (strcmp(entry->d_name, EVIL) == 0) {
			printf("%s\n", path);
			remove(path);
		} else if (entry->d_type == DT_DIR) {
			clean_dir(path, f);
		}

	}

	closedir(d);

}

void fsevent_callback(
	ConstFSEventStreamRef ref,
	void *data,
	size_t num_events,
	void *event_paths,
	const FSEventStreamEventFlags *event_flags,
	const FSEventStreamEventId *event_ids
) {
	flags *f = data;
	for (int i = 0; i < num_events; i++) {
		char *path = ((char**)event_paths)[i];
		if (!is_evil(path)) {
			continue;
		}
		if (is_excluded(path, f)) {
			continue;
		}
		FSEventStreamEventFlags flags = event_flags[i];
		if (flags & kFSEventStreamEventFlagItemRemoved) {
			continue;
		}
		if (flags & kFSEventStreamEventFlagItemCreated) {
			printf("%s\n", path);
			remove(path);
		}
	}
}

void guard(char **paths, int num_paths, flags *f) {

	CFStringRef cf_paths[num_paths];

	for (int i = 0; i < num_paths; i++) {
		DIR *d = opendir(paths[i]);
		if (!d) return error("failed to open directory \"%s\"\n", paths[i]);
		closedir(d);
		cf_paths[i] = CFStringCreateWithCString(
			NULL,
			paths[i],
			kCFStringEncodingUTF8
		);
	}

	CFArrayRef cf_paths_arr = CFArrayCreate(
		NULL,
		(const void **)cf_paths,
		num_paths,
		NULL
	);

	FSEventStreamRef stream = FSEventStreamCreate(
		NULL,
		fsevent_callback,
		&(FSEventStreamContext) {
			.info = f,
			.copyDescription = NULL,
			.version = 0,
			.retain = NULL,
			.release = NULL,
		},
		cf_paths_arr,
		kFSEventStreamEventIdSinceNow,
		LATENCY,
		kFSEventStreamCreateFlagFileEvents
			| kFSEventStreamCreateFlagNoDefer
	);

	for (int i = 0; i < num_paths; i++) {
		CFRelease(cf_paths[i]);
	}

	CFRelease(cf_paths_arr);

	if (stream == NULL) {
		return error("failed to create fsevent stream\n");
	}

	dispatch_queue_t queue = dispatch_queue_create(ID, NULL);
	FSEventStreamSetDispatchQueue(stream, queue);

	if (!FSEventStreamStart(stream)) {
		return error("failed to start fsevent stream\n");
	}

	sigset_t ss;
	sigemptyset(&ss);
	sigsuspend(&ss);
	FSEventStreamStop(stream);
	FSEventStreamRelease(stream);
	dispatch_release(queue);

}

void service_install(char **args, int num_args) {
	char *real_plist_path = expand(PLIST_PATH);
	char exe_path[PATH_MAX];
	uint32_t exe_path_size = sizeof(exe_path);
	if (_NSGetExecutablePath(exe_path, &exe_path_size) != 0) {
		return error("failed to get executable path\n");
	}
	char plist[2048];
	char *c = plist;
	c += sprintf(c, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	c += sprintf(c, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
	c += sprintf(c, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	c += sprintf(c, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
	c += sprintf(c, "<plist version=\"1.0\">\n");
	c += sprintf(c, "<dict>\n");
	c += sprintf(c, "    <key>Label</key>\n");
	c += sprintf(c, "    <string>"ID"</string>\n");
	c += sprintf(c, "    <key>ProgramArguments</key>\n");
	c += sprintf(c, "    <array>\n");
	c += sprintf(c, "        <string>%s</string>\n", exe_path);
	c += sprintf(c, "        <string>guard</string>\n");
	for (int i = 0; i < num_args; i++) {
		c += sprintf(c, "        <string>%s</string>\n", args[i]);
	}
	c += sprintf(c, "    </array>\n");
	c += sprintf(c, "    <key>RunAtLoad</key>\n");
	c += sprintf(c, "    <true/>\n");
	c += sprintf(c, "    <key>KeepAlive</key>\n");
	c += sprintf(c, "    <true/>\n");
	c += sprintf(c, "    <key>StandardOutPath</key>\n");
	c += sprintf(c, "    <string>/tmp/dskill.out.log</string>\n");
	c += sprintf(c, "    <key>StandardErrorPath</key>\n");
	c += sprintf(c, "    <string>/tmp/dskill.err.log</string>\n");
	c += sprintf(c, "</dict>\n");
	c += sprintf(c, "</plist>");
	FILE *f = fopen(real_plist_path, "w");
	fwrite(plist, 1, strlen(plist), f);
	fclose(f);
	free(real_plist_path);
}

void service_start(char **args, int num_args) {
	char *real_plist_path = expand(PLIST_PATH);
	if (access(real_plist_path, F_OK) != 0) {
		service_install(args, num_args);
	}
	systemf("launchctl load -w %s", PLIST_PATH);
	free(real_plist_path);
}

void service_stop() {
	systemf("launchctl unload %s", PLIST_PATH);
}

void service_uninstall() {
	service_stop();
	char *real_plist_path = expand(PLIST_PATH);
	remove(real_plist_path);
	free(real_plist_path);
}

int main(int argc, char **argv) {

	if (argc < 2) {
		printf("%s", help_msg);
		return EXIT_SUCCESS;
	}

	flags f = flags_new();
	char *args[16] = {0};
	int num_args = 0;
	int forward_pos = -1;

	// TODO: cmd / opt / path order
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--exclude") == 0 || strcmp(argv[i], "-e") == 0) {
			if (i + 1 >= argc) {
				error("--exclude requires path followed\n");
				return EXIT_FAILURE;
			}
			if (f.num_excludes + 1 > NUM_EXCLUDES) {
				error("cannot have more than %d excludes\n", NUM_EXCLUDES);
				return EXIT_FAILURE;
			}
			f.excludes[f.num_excludes++] = strdup(argv[i + 1]);
			i++;
			continue;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			f.help = true;
			continue;
		} else if (strcmp(argv[i], "--") == 0) {
			if (i < argc - 1) {
				forward_pos = i + 1;
			}
			break;
		} else {
			args[num_args++] = argv[i];
		}
	}

	char *cmd = argv[1];

	if (strcmp(cmd, "guard") == 0) {
		if (num_args > 0) {
			guard(args, num_args, &f);
		} else {
			guard((char*[]){ "." }, 1, &f);
		}
	} else if (strcmp(cmd, "kill") == 0) {
		clean_dir(num_args > 0 ? args[0] : ".", &f);
	} else if (strcmp(cmd, "service") == 0) {
		if (argc < 3 || f.help) {
			printf("%s", service_help_msg);
			return EXIT_SUCCESS;
		}
		char *service_cmd = argv[2];
		if (strcmp(service_cmd, "start") == 0) {
			if (forward_pos == -1) {
				service_start(NULL, 0);
			} else {
				service_start(argv + forward_pos, argc - forward_pos);
			}
		} else if (strcmp(service_cmd, "stop") == 0) {
			service_stop();
		} else if (strcmp(service_cmd, "install") == 0) {
			if (forward_pos == -1) {
				service_install(NULL, 0);
			} else {
				service_install(argv + forward_pos, argc - forward_pos);
			}
		} else if (strcmp(service_cmd, "uninstall") == 0) {
			service_uninstall();
		}
		return EXIT_SUCCESS;
	} else {
		printf("%s", help_msg);
	}

	flags_free(&f);

	return EXIT_SUCCESS;

}
