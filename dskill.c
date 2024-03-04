#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <CoreServices/CoreServices.h>
#include <Foundation/Foundation.h>

#define EVIL ".DS_Store"
#define LATENCY 0.5
#define NANOSEC 1000000000
#define LAUNCHCTL_BIN   "/bin/launchctl"
#define PLIST_NAME "xyz.space55.dskill"
#define PLIST_PATH "%s/Library/LaunchAgents/"PLIST_NAME".plist"

#define PLIST_CONTENT \
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
	"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" \
	"<plist version=\"1.0\">\n" \
	"<dict>\n" \
	"    <key>Label</key>\n" \
	"    <string>"PLIST_NAME"</string>\n" \
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


bool is_evil(char *path) {
	return strcmp(path + strlen(path) - strlen(EVIL), EVIL) == 0;
}

void help() {
	printf("Fuck .DS_Store\n");
	printf("\n");
	printf("USAGE\n");
	printf("  $ dskill <cmd>\n");
	printf("\n");
	printf("CMD\n");
	printf("  kill      delete all .DS_Store now\n");
	printf("  guard     start daemon and prevent creation of .DS_Store\n");
}

void help_service() {
	printf("Manage dskill guard service\n");
	printf("\n");
	printf("USAGE\n");
	printf("  $ dskill service <cmd>\n");
	printf("\n");
	printf("CMD\n");
	printf("  start       start the service\n");
	printf("  stop        stop the service\n");
}

void error(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

void warn(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void fsevent_callback(
	ConstFSEventStreamRef ref,
	void *client_callBack_info,
	size_t num_events,
	void *event_paths,
	const FSEventStreamEventFlags *event_flags,
	const FSEventStreamEventId *event_ids
) {

	for (int i = 0; i < num_events; i++) {

		char *path = ((char**)event_paths)[i];

		if (!is_evil(path)) {
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

int guard(char *path) {

	CFStringRef p = CFStringCreateWithCString(
		NULL,
		path,
		kCFStringEncodingUTF8
	);

	CFArrayRef paths = CFArrayCreate(NULL, (const void **)&p, 1, NULL);

	FSEventStreamRef stream = FSEventStreamCreate(
		NULL,
		fsevent_callback,
		NULL,
		paths,
		kFSEventStreamEventIdSinceNow,
		LATENCY,
		kFSEventStreamCreateFlagFileEvents
			| kFSEventStreamCreateFlagNoDefer
	);

	CFRelease(paths);

	if (stream == NULL) {
		error("failed to create fsevent stream\n");
		return EXIT_FAILURE;
	}

	dispatch_queue_t queue = dispatch_queue_create("dskill_queue", NULL);
	FSEventStreamSetDispatchQueue(stream, queue);

	if (!FSEventStreamStart(stream)) {
		error("failed to start fsevent stream\n");
		return EXIT_FAILURE;
	}

	for (;;) {
		nanosleep(&(struct timespec){
			.tv_sec = 0,
			.tv_nsec = LATENCY * NANOSEC,
		}, NULL);
    }

	FSEventStreamStop(stream);
	FSEventStreamRelease(stream);
	dispatch_release(queue);

	return EXIT_SUCCESS;

}

void clean_dir(char *dir) {

	DIR *d = opendir(dir);
	struct dirent *entry;

	if (!d) {
		warn("failed to open %s\n", dir);
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
			clean_dir(path);
		}

	}

	closedir(d);

}

void service_start() {

	char exe_path[PATH_MAX];
	char plist_path[PATH_MAX];
	uint32_t exe_path_size = sizeof(exe_path);

	if (_NSGetExecutablePath(exe_path, &exe_path_size) != 0) {
		error("failed to get executable path\n");
	}

	sprintf(plist_path, PLIST_PATH, [NSHomeDirectory() UTF8String]);

	if (access(plist_path, F_OK) != 0) {
		FILE *f = fopen(plist_path, "w");
		fprintf(f, PLIST_CONTENT, exe_path);
		fclose(f);
	}

	char cmd[1024];
	sprintf(cmd, "launchctl load -w %s", plist_path);
	system(cmd);

}

void service_stop() {
	char plist_path[PATH_MAX];
	sprintf(plist_path, PLIST_PATH, [NSHomeDirectory() UTF8String]);
	char cmd[1024];
	sprintf(cmd, "launchctl unload %s", plist_path);
	system(cmd);
}

int main(int argc, char **argv) {

	if (argc < 2) {
		help();
		return EXIT_SUCCESS;
	}

	char *cmd = argv[1];

	if (strcmp(cmd, "guard") == 0) {
		return guard(".");
	} else if (strcmp(cmd, "kill") == 0) {
		clean_dir(".");
	} else if (strcmp(cmd, "service") == 0) {
		if (argc < 3) {
			help_service();
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

	return EXIT_SUCCESS;

}
