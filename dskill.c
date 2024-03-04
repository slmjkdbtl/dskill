#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <time.h>
#include <CoreServices/CoreServices.h>

#define EVIL ".DS_Store"
#define NANOSEC 1000000000
#define LATENCY 0.5

bool is_evil(char *path) {
	return strcmp(path + strlen(path) - strlen(EVIL), EVIL) == 0;
}

void help() {
	printf("Fuck .DS_Store\n");
	printf("\n");
	printf("USAGE\n");
	printf("\n");
	printf("  $ dskill <cmd>\n");
	printf("\n");
	printf("CMD\n");
	printf("\n");
	printf("  kill      delete all .DS_Store now\n");
	printf("  guard     start daemon and prevent creation of .DS_Store forever\n");
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

int guard() {

	CFStringRef path = CFSTR(".");
	CFArrayRef paths = CFArrayCreate(NULL, (const void **)&path, 1, NULL);

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
		printf("failed to start fsevent stream\n");
		return EXIT_FAILURE;
	}

	dispatch_queue_t queue = dispatch_queue_create("dskill_queue", NULL);
	FSEventStreamSetDispatchQueue(stream, queue);

	if (!FSEventStreamStart(stream)) {
		printf("failed to start fsevent stream\n");
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

char *join_path(char *p1, char *p2) {
	if (strcmp(p1, ".") == 0) return strdup(p2);
	char *path = malloc(strlen(p1) + strlen(p2) + 2);
	sprintf(path, "%s/%s", p1, p2);
	return path;
}

void clean_dir(char *dir) {
	DIR *d = opendir(dir);
	if (!d) {
		printf("failed to open %s\n", dir);
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(d))) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (strcmp(entry->d_name, EVIL) == 0) {
			char *path = join_path(dir, EVIL);
			printf("%s\n", path);
			remove(path);
			free(path);
		} else if (entry->d_type == DT_DIR) {
			char *path = join_path(dir, entry->d_name);
			clean_dir(path);
			free(path);
		}
	}
	closedir(d);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		help();
		return EXIT_SUCCESS;
	}
	char *cmd = argv[1];
	if (strcmp(cmd, "guard") == 0) {
		return guard();
	} else if (strcmp(cmd, "kill") == 0) {
		clean_dir(".");
		return EXIT_SUCCESS;
	} else {
		help();
	}
	return EXIT_SUCCESS;
}
