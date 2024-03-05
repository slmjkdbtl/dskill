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
#define LATENCY 0.5
#define ID "xyz.space55.dskill"
#define PLIST_PATH "~/Library/LaunchAgents/"ID".plist"

#define PLIST_CONTENT \
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
	"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" \
	"<plist version=\"1.0\">\n" \
	"<dict>\n" \
	"    <key>Label</key>\n" \
	"    <string>"ID"</string>\n" \
	"    <key>ProgramArguments</key>\n" \
	"    <array>\n" \
	"        <string>%s</string>\n" \
	"        <string>guard</string>\n" \
	"    </array>\n" \
	"    <key>RunAtLoad</key>\n" \
	"    <true/>\n" \
	"    <key>KeepAlive</key>\n" \
	"    <true/>\n" \
	"    <key>StandardOutPath</key>\n" \
	"    <string>/tmp/dskill.out.log</string>\n" \
	"    <key>StandardErrorPath</key>\n" \
	"    <string>/tmp/dskill.err.log</string>\n" \
	"</dict>\n" \
	"</plist>"

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
"  $ dskill service <cmd>\n"
"\n"
"COMMANDS\n"
"\n"
"  start     start the service, run `dskill guard` in the background\n"
"  stop      stop the service\n";

typedef struct flags {
	int num_excludes;
	char *excludes[64];
} flags;

void flags_free(flags *f) {
	for (int i = 0; i < f->num_excludes; i++) {
		free(f->excludes[i]);
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
		if (strcmp(dir, pat) == 0) {
			return true;
		}
	}
	free(path2);
	return false;
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

void guard(char *path, flags *f) {

	DIR *d = opendir(path);
	if (!d) return error("failed to open directory \"%s\"\n", path);
	closedir(d);

	CFStringRef p = CFStringCreateWithCString(
		NULL,
		path,
		kCFStringEncodingUTF8
	);

	CFArrayRef paths = CFArrayCreate(NULL, (const void **)&p, 1, NULL);

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
		paths,
		kFSEventStreamEventIdSinceNow,
		LATENCY,
		kFSEventStreamCreateFlagFileEvents
			| kFSEventStreamCreateFlagNoDefer
	);

	CFRelease(paths);

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

// TODO: how to pass flags to service command, config file?
void service_install() {
	char *real_plist_path = expand(PLIST_PATH);
	char exe_path[PATH_MAX];
	uint32_t exe_path_size = sizeof(exe_path);
	if (_NSGetExecutablePath(exe_path, &exe_path_size) != 0) {
		return error("failed to get executable path\n");
	}
	FILE *f = fopen(real_plist_path, "w");
	fprintf(f, PLIST_CONTENT, exe_path);
	fclose(f);
	free(real_plist_path);
}

void service_start() {
	char *real_plist_path = expand(PLIST_PATH);
	if (access(real_plist_path, F_OK) != 0) {
		service_install();
	}
	systemf("launchctl load -w %s", PLIST_PATH);
	free(real_plist_path);
}

void service_stop() {
	systemf("launchctl unload %s", PLIST_PATH);
}

int main(int argc, char **argv) {

	if (argc < 2) {
		printf("%s", help_msg);
		return EXIT_SUCCESS;
	}

	flags f = {
		.num_excludes = 0,
		.excludes = {},
	};

	// TODO: cmd / opt / path order
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--exclude") == 0) {
			if (i + 1 >= argc) {
				error("--exclude requires path followed\n");
				return EXIT_FAILURE;
			}
			f.excludes[f.num_excludes++] = expand(argv[i + 1]);
			i++;
			continue;
		}
	}

	char *cmd = argv[1];

	if (strcmp(cmd, "guard") == 0) {
		guard(argc >= 3 ? argv[2] : ".", &f);
	} else if (strcmp(cmd, "kill") == 0) {
		clean_dir(argc >= 3 ? argv[2] : ".", &f);
	} else if (strcmp(cmd, "service") == 0) {
		if (argc < 3) {
			printf("%s", service_help_msg);
			return EXIT_SUCCESS;
		}
		char *service_cmd = argv[2];
		if (strcmp(service_cmd, "start") == 0) {
			service_start();
		} else if (strcmp(service_cmd, "stop") == 0) {
			service_stop();
		}
		return EXIT_SUCCESS;
	}

	flags_free(&f);

	return EXIT_SUCCESS;

}
