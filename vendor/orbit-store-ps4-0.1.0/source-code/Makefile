.DEFAULT_GOAL := host
SOURCES := $(wildcard backend/*.c) .deps/cjson/cJSON.c
HEADERS := $(wildcard backend/*.h)
CPPFLAGS := -D_FILE_OFFSET_BITS=64 -Ibackend -Ilauncher -I.deps/cjson -I.deps/sqlite -Ibuild/generated
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wno-misleading-indentation
HOST_CC ?= clang-19
HOST_CXX ?= clang++-19
PS4_PAYLOAD_SDK ?= /opt/ps4-payload-sdk
PS4_CC := $(PS4_PAYLOAD_SDK)/bin/orbis-clang
PS4_LIB := $(PS4_PAYLOAD_SDK)/target/user/homebrew
PS4_STRIP := $(PS4_PAYLOAD_SDK)/bin/orbis-strip
GENERATED := build/generated/assets.h build/generated/catalog.h build/generated/ca.h build/generated/network.h build/generated/launcher.h build/generated/messages.h
PAYLOAD_SOURCES := $(SOURCES) launcher/install.c launcher/platform_ps4.c launcher/package_image.c launcher/loader_images.c
# Embedded by launcher/loader_images.c: the starter payload managers run, and elfldr.
LOADER_IMAGES := build/orbit_starter.elf .deps/elfldr/elfldr.elf
# Unused code is left out (as the browser shortcut's link): the public payload carries the
# runtime too and both must stay within the loaders' 64 MiB.
PAYLOAD_CC = $(PS4_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_INSTALL_LAUNCHER -Wno-unreachable-code-generic-assoc -I$(PS4_LIB)/include -ffunction-sections -fdata-sections -Wl,--gc-sections
# AppInstUtil registers the home-screen icon for the runtime Orbit installs.
PS4_LAUNCHER_LIBS :=
PS4_ARCHIVE_DEPS := build/sqlite-ps4.o build/sevenzip-ps4.o .deps/7zip-ps4/liborbit7zip.a build/unrar-ps4.o .deps/unrar-ps4/liborbitunrar.a
HOST_ARCHIVE_DEPS := build/sqlite-host.o build/sevenzip-host.o .deps/7zip-host/liborbit7zip.a build/unrar-host.o .deps/unrar-host/liborbitunrar.a
ARCHIVE_LIBS := build/sqlite-ps4.o -larchive -llzma -lbz2 -lz build/sevenzip-ps4.o -Wl,--whole-archive .deps/7zip-ps4/liborbit7zip.a -Wl,--no-whole-archive build/unrar-ps4.o .deps/unrar-ps4/liborbitunrar.a -lc++ -lc++abi -lunwind
HOST_ARCHIVE_LIBS := build/sqlite-host.o -L/usr/local/lib -l:libarchive.a -llzma -lbz2 -lz build/sevenzip-host.o -Wl,--whole-archive .deps/7zip-host/liborbit7zip.a -Wl,--no-whole-archive build/unrar-host.o .deps/unrar-host/liborbitunrar.a -lstdc++
PAYLOAD_LIBS := -L$(PS4_LIB)/lib -lcurl -lmicrohttpd -lssl -lcrypto $(ARCHIVE_LIBS) $(PS4_LAUNCHER_LIBS)
# backend/ps4_fs.c: the SDK's *at calls; backend/ps4_dns.c: host lookups
# without the PS4 resolver, which aborts the process now and then.
PS4_WRAPS := -Wl,--wrap=openat -Wl,--wrap=mkdirat -Wl,--wrap=fstatat -Wl,--wrap=linkat -Wl,--wrap=renameat -Wl,--wrap=unlinkat -Wl,--wrap=futimens -Wl,--wrap=getaddrinfo -Wl,--wrap=strerror
.PHONY: host payload payload-core test-backend test-launcher test-payload-imports test-updates
SQLITE_FLAGS := -O2 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_THREADSAFE=1 -DSQLITE_TEMP_STORE=3 -DSQLITE_DEFAULT_MEMSTATUS=0
.deps/sqlite/sqlite3.c .deps/sqlite/sqlite3.h &: tools/build-sqlite.py
	python3 tools/build-sqlite.py
build/sqlite-host.o: .deps/sqlite/sqlite3.c .deps/sqlite/sqlite3.h Makefile
	@mkdir -p build
	$(HOST_CC) $(SQLITE_FLAGS) -c $< -o $@
build/sqlite-ps4.o: .deps/sqlite/sqlite3.c .deps/sqlite/sqlite3.h Makefile
	@mkdir -p build
	$(PS4_CC) $(SQLITE_FLAGS) -c $< -o $@
.deps/7zip-host/liborbit7zip.a: tools/build-7zip.sh tools/7zip-orbit.mak tools/patch-7zip.py
	bash tools/build-7zip.sh host
.deps/7zip-ps4/liborbit7zip.a: tools/build-7zip.sh tools/7zip-orbit.mak tools/patch-7zip.py
	bash tools/build-7zip.sh ps4
build/sevenzip-host.o: backend/sevenzip.cpp backend/sevenzip.h .deps/7zip-host/liborbit7zip.a
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -Werror -I.deps/7zip/CPP -c $< -o $@
build/sevenzip-ps4.o: backend/sevenzip.cpp backend/sevenzip.h .deps/7zip-ps4/liborbit7zip.a
	$(PS4_PAYLOAD_SDK)/bin/orbis-clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I.deps/7zip/CPP -c $< -o $@
# RARLAB's UnRAR for RAR archives (multithreaded unpacking); see tools/build-unrar.sh.
UNRAR_FLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-dangling-else -Wno-logical-op-parentheses -Wno-switch -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-deprecated-copy -Wno-missing-field-initializers -Wno-sign-compare -Ibackend -isystem .deps/unrar -DRARDLL -DRAR_SMP -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
.deps/unrar-host/liborbitunrar.a: tools/build-unrar.sh tools/patch-unrar.py
	bash tools/build-unrar.sh host
.deps/unrar-ps4/liborbitunrar.a: tools/build-unrar.sh tools/patch-unrar.py
	bash tools/build-unrar.sh ps4
build/unrar-host.o: backend/unrar.cpp backend/unrar.h backend/sevenzip.h .deps/unrar-host/liborbitunrar.a
	@mkdir -p build
	$(HOST_CXX) $(UNRAR_FLAGS) -DORBIT_DESKTOP -c $< -o $@
build/unrar-ps4.o: backend/unrar.cpp backend/unrar.h backend/sevenzip.h .deps/unrar-ps4/liborbitunrar.a
	@mkdir -p build
	$(PS4_PAYLOAD_SDK)/bin/orbis-clang++ $(UNRAR_FLAGS) -c $< -o $@
build/orbit-host build/orbit-test build/orbit-api-recovery-test build/orbit-transfer-benchmark-test: $(HOST_ARCHIVE_DEPS)
build/orbit_runtime.elf build/orbit_store.elf build/orbit_core.elf build/orbit_transfer_benchmark.elf: $(PS4_ARCHIVE_DEPS)
build/generated/messages.h: tools/embed-messages.py backend/notify.c $(wildcard i18n/*.json)
	python3 tools/embed-messages.py
build/generated/network.h build/generated/launcher.h &: tools/embed-launcher.py config/network.json launcher/sce_sys/param.json launcher/sce_sys/icon0.png
	python3 tools/embed-launcher.py
host: build/orbit-host
build/orbit-host: $(SOURCES) $(HEADERS) $(GENERATED)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP $(SOURCES) -o $@ -lcurl -lmicrohttpd -lssl -lcrypto $(HOST_ARCHIVE_LIBS) -lpthread -lm
build/orbit-test: $(SOURCES) $(HEADERS) $(GENERATED)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -DORBIT_TEST $(SOURCES) -o $@ -lcurl -lmicrohttpd -lssl -lcrypto $(HOST_ARCHIVE_LIBS) -lpthread -lm
payload: build/orbit_store.elf
# The runtime is the copy saved on the console for payload managers to start.
build/orbit_runtime.elf: build/orbit-browser.pkg $(LOADER_IMAGES) $(PAYLOAD_SOURCES) $(HEADERS) launcher/install.h $(GENERATED) Makefile
	$(PAYLOAD_CC) $(PAYLOAD_SOURCES) -o $@ $(PAYLOAD_LIBS) $(PS4_WRAPS)
	$(PS4_STRIP) --strip-unneeded $@
	python3 -c 'import os; p="$@"; n=os.path.getsize(p); assert n <= 64*1024*1024, f"{p}: {n} bytes exceeds the 64 MiB loader/update limit"'
# The public payload: the same server and icon setup, carrying the runtime it saves.
build/orbit_store.elf: build/orbit-browser.pkg $(LOADER_IMAGES) $(PAYLOAD_SOURCES) $(HEADERS) launcher/install.h launcher/runtime_image.c $(GENERATED) build/orbit_runtime.elf Makefile
	$(PAYLOAD_CC) -DORBIT_EMBED_RUNTIME $(PAYLOAD_SOURCES) launcher/runtime_image.c -o $@ $(PAYLOAD_LIBS) $(PS4_WRAPS)
	$(PS4_STRIP) --strip-unneeded $@
	python3 -c 'import os; p="$@"; n=os.path.getsize(p); assert n <= 64*1024*1024, f"{p}: {n} bytes exceeds the 64 MiB loader/update limit"'
# The starter GoldHEN's LaunchPad, AutoRun and Homebrew Launcher run (launcher/starter.c).
build/orbit_starter.elf: launcher/starter.c backend/loader_start.c backend/loader_start.h Makefile
	@mkdir -p build
	$(PS4_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ibackend launcher/starter.c backend/loader_start.c -o $@
	$(PS4_STRIP) --strip-unneeded $@
.deps/elfldr/elfldr.elf:
	@echo "Missing $@: run tools/bootstrap.py (pinned elfldr a481740, built by the SDK image)." >&2; exit 1
payload-core: build/orbit_core.elf
build/orbit_core.elf: $(SOURCES) $(HEADERS) $(GENERATED) Makefile
	$(PS4_CC) $(CPPFLAGS) $(CFLAGS) -Wno-unreachable-code-generic-assoc -I$(PS4_LIB)/include $(SOURCES) -o $@ -L$(PS4_LIB)/lib -lcurl -lmicrohttpd -lssl -lcrypto $(ARCHIVE_LIBS) $(PS4_WRAPS)
test-updates: build/orbit-host build/orbit-test
	python3 tests/update_integration.py

test-backend: build/orbit-host build/orbit-test
	python3 tests/backend_integration.py

.PHONY: test-storage
build/orbit-storage-test: backend/storage.c backend/orbit.h tests/storage_driver.c .deps/cjson/cJSON.c Makefile
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/storage.c tests/storage_driver.c .deps/cjson/cJSON.c -Wl,--wrap=lstat64 -Wl,--wrap=stat64 -Wl,--wrap=statvfs64 -Wl,--wrap=access -Wl,--wrap=open64 -Wl,--wrap=openat64 -Wl,--wrap=mkdirat -Wl,--wrap=fstat64 -Wl,--wrap=fstatat64 -Wl,--wrap=unlinkat -Wl,--wrap=write -Wl,--wrap=close -o $@ -lpthread -lm
test-storage: build/orbit-storage-test
	./build/orbit-storage-test

.PHONY: test-art
test-art: build/orbit-test
	python3 tests/art_integration.py

.PHONY: test-tv-app
test-tv-app: build/orbit-test
	python3 tests/tvapp_integration.py

.PHONY: test-package-platform
build/orbit-package-platform-test: backend/package_platform.c backend/ps4_modules.c backend/ps4_modules.h backend/packages.h backend/ps4_bgft.h tests/package_platform_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -D_DEFAULT_SOURCE -Itests/stubs backend/package_platform.c backend/ps4_modules.c tests/package_platform_driver.c -Wl,--wrap=dlsym -ldl -lpthread -o $@
test-package-platform: build/orbit-package-platform-test
	./build/orbit-package-platform-test

build/orbit-module-memory-test: backend/ps4_modules.c backend/ps4_modules.h tests/ps4_module_memory_driver.c
	$(HOST_CC) $(CFLAGS) -D_DEFAULT_SOURCE -DORBIT_DESKTOP -Ibackend backend/ps4_modules.c tests/ps4_module_memory_driver.c -o $@
.PHONY: test-module-memory
test-module-memory: build/orbit-module-memory-test
	./build/orbit-module-memory-test

.PHONY: test-goldhen-autorun
build/orbit-goldhen-autorun-test: backend/goldhen_autorun.c backend/goldhen_autorun.h tests/goldhen_autorun_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) backend/goldhen_autorun.c tests/goldhen_autorun_driver.c -o $@
test-goldhen-autorun: build/orbit-goldhen-autorun-test
	./build/orbit-goldhen-autorun-test

.PHONY: test-https
build/orbit-https-test: backend/https.c backend/https.h tests/https_driver.c build/generated/ca.h Makefile
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_TEST backend/https.c tests/https_driver.c -o $@ -lcurl -lssl -lcrypto -lpthread
test-https: build/orbit-https-test
	python3 tests/https_integration.py
.PHONY: test-dns-message
build/orbit-dns-message-test: backend/dns_message.c backend/dns_message.h tests/dns_message_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/dns_message.c tests/dns_message_driver.c -o $@
test-dns-message: build/orbit-dns-message-test
	./build/orbit-dns-message-test
.PHONY: test-ps4-strerror
build/orbit-ps4-strerror-test: backend/ps4_strerror.c tests/ps4_strerror_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_STRERROR_TEST -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/ps4_strerror.c tests/ps4_strerror_driver.c -o $@
test-ps4-strerror: build/orbit-ps4-strerror-test
	./build/orbit-ps4-strerror-test

.PHONY: test-announcements
test-announcements: build/orbit-test
	python3 tests/announcements_integration.py

.PHONY: test-firmware-notify
test-firmware-notify: build/orbit-test
	python3 tests/firmware_notify_integration.py

.PHONY: test-ntfy
test-ntfy: build/orbit-test
	python3 tests/ntfy_integration.py

.PHONY: test-diagnostics
test-diagnostics: build/orbit-test
	python3 tests/diagnostics_integration.py

.PHONY: test-api-recovery
build/orbit-api-recovery-test: $(SOURCES) $(HEADERS) $(GENERATED) tests/api_recovery_faults.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -DORBIT_TEST $(SOURCES) tests/api_recovery_faults.c -o $@ -Wl,--wrap=MHD_run_wait -Wl,--wrap=MHD_start_daemon -Wl,--wrap=connect -Wl,--wrap=getsockname -lcurl -lmicrohttpd -lssl -lcrypto $(HOST_ARCHIVE_LIBS) -lpthread -lm
test-api-recovery: build/orbit-api-recovery-test
	python3 tests/api_recovery_integration.py

.PHONY: test-process-identity
build/orbit-process-identity-test: backend/process_identity.c backend/process_identity.h tests/process_identity_driver.c tests/stubs/ps4/kernel.h Makefile
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_PROCESS_IDENTITY_EXPERIMENT -Itests/stubs backend/process_identity.c tests/process_identity_driver.c -o $@
build/orbit-process-identity-default-test: backend/process_identity.c backend/process_identity.h tests/process_identity_default.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) backend/process_identity.c tests/process_identity_default.c -o $@
test-process-identity: build/orbit-process-identity-test build/orbit-process-identity-default-test
	./build/orbit-process-identity-test
	./build/orbit-process-identity-default-test

.PHONY: test-ranges
.PHONY: test-debrid
build/orbit-debrid-network-test: backend/debrid_network.c backend/debrid.h backend/provider_network.h tests/debrid_network_driver.c Makefile
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP backend/debrid_network.c tests/debrid_network_driver.c -o $@ -lcurl
test-debrid: build/orbit-test build/orbit-debrid-network-test
	./build/orbit-debrid-network-test
	python3 tests/debrid_integration.py

.PHONY: test-archive-account
test-archive-account: build/orbit-test
	python3 tests/archive_account_integration.py

test-ranges: build/orbit-test
	python3 tests/range_integration.py
	python3 tests/partial_retry_integration.py
.PHONY: test-extract test-sevenzip-read
build/orbit-sevenzip-read-test: tests/sevenzip_read_driver.cpp build/sevenzip-host.o .deps/7zip-host/liborbit7zip.a
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -Werror -Ibackend $< build/sevenzip-host.o -Wl,--whole-archive .deps/7zip-host/liborbit7zip.a -Wl,--no-whole-archive -Wl,--wrap=read -o $@
test-sevenzip-read: build/orbit-sevenzip-read-test
	python3 tests/sevenzip_read_integration.py
test-extract: build/orbit-test test-sevenzip-read
	python3 tests/extract_integration.py
	python3 tests/receive_folder_integration.py

.PHONY: test-transfer-writer
build/orbit-transfer-writer-test: backend/transfer_writer.c backend/transfer_writer.h backend/orbit.h tests/transfer_writer_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/transfer_writer.c tests/transfer_writer_driver.c -Wl,--wrap=fsync -Wl,--wrap=pwrite64 -o $@ -lpthread
build/orbit-transfer-writer-schedule-test: backend/transfer_writer.c backend/transfer_writer.h backend/orbit.h tests/transfer_writer_schedule_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -DORBIT_BENCHMARK -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/transfer_writer.c tests/transfer_writer_schedule_driver.c -Wl,--wrap=pwrite64 -Wl,--wrap=pthread_cond_timedwait -o $@ -lpthread
test-transfer-writer: build/orbit-transfer-writer-test build/orbit-transfer-writer-schedule-test
	./build/orbit-transfer-writer-test
	./build/orbit-transfer-writer-schedule-test

.PHONY: benchmark-transfer
benchmark-transfer: build/orbit-test
	python3 tests/transfer_benchmark.py

# Explicit developer diagnostic; never a release dependency. The random secret
# header is generated locally under ignored build/generated before compilation.
BENCH_SOURCES := $(filter-out backend/main.c backend/api.c,$(SOURCES)) experiments/transfer-benchmark/main.c
.PHONY: transfer-benchmark-ps4 test-transfer-benchmark
transfer-benchmark-ps4: build/orbit_transfer_benchmark.elf
build/orbit_transfer_benchmark.elf: $(BENCH_SOURCES) $(HEADERS) $(GENERATED) build/generated/benchmark-secret.h Makefile
	$(PS4_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_BENCHMARK -Wno-unreachable-code-generic-assoc -I$(PS4_LIB)/include $(BENCH_SOURCES) -o $@ -L$(PS4_LIB)/lib -lcurl -lmicrohttpd -lssl -lcrypto $(ARCHIVE_LIBS) $(PS4_WRAPS)
build/orbit-transfer-benchmark-test: $(BENCH_SOURCES) $(HEADERS) $(GENERATED) build/generated/benchmark-secret.h Makefile
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -D_DEFAULT_SOURCE -DORBIT_DESKTOP -DORBIT_TEST -DORBIT_BENCHMARK $(BENCH_SOURCES) -o $@ -lcurl -lmicrohttpd -lssl -lcrypto $(HOST_ARCHIVE_LIBS) -lpthread -lm
test-transfer-benchmark: build/orbit-transfer-benchmark-test
	python3 tests/console_benchmark_integration.py

.PHONY: test-viking
build/orbit-providers-test: backend/providers.c backend/orbit.h tests/providers_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP backend/providers.c tests/providers_driver.c -o $@
test-viking: build/orbit-test build/orbit-providers-test
	./build/orbit-providers-test
	python3 tests/viking_integration.py
build/orbit-launcher-platform-test: launcher/platform_ps4.c launcher/install.h tests/launcher_platform_driver.c tests/stubs/ps4/kernel.h
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) launcher/platform_ps4.c tests/launcher_platform_driver.c -o $@
build/orbit-launcher-browser-test: launcher/browser.c tests/launcher_browser_driver.c build/generated/network.h
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/launcher_browser_driver.c -o $@
.PHONY: test-starter
build/orbit-starter-test: launcher/starter.c backend/loader_start.c backend/loader_start.h
	@mkdir -p build
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -DORBIT_STARTER_TEST -Ibackend launcher/starter.c backend/loader_start.c -o $@
test-starter: build/orbit-starter-test
	python3 tests/starter_integration.py
test-launcher: build/orbit-launcher-platform-test build/orbit-launcher-browser-test
	python3 tests/launcher_platform.py
	python3 tests/launcher_browser.py
test-payload-imports: payload payload-core
	python3 tests/payload_imports.py

.PHONY: test-catalog-refresh
test-catalog-refresh: build/orbit-test
	python3 tests/catalog_refresh.py

.PHONY: test-files
test-files: build/orbit-test
	python3 tests/files_integration.py

.PHONY: test-game-updates
# Catalogue updates, Library's Update available and Get everything (bundles).
test-game-updates: build/orbit-test
	python3 tests/game_updates_integration.py

.PHONY: test-library
test-library: build/orbit-host build/orbit-test
	python3 tests/library_integration.py
	python3 tests/library_actions.py
	python3 tests/library_verified_move.py

# The page scanner's pattern search (browser_scan.c) stays for Vikingfile page
# checks. Nothing reads another process's memory any more: provider links come
# from Orbit Store Browser through Sony's browser window.
.PHONY: test-browser-probe
build/orbit-browser-probe-test: backend/browser_scan.c backend/browser_scan.h tests/browser_probe_driver.c
	$(HOST_CC) $(CFLAGS) -Ibackend -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/browser_scan.c tests/browser_probe_driver.c -o $@
test-browser-probe: build/orbit-browser-probe-test
	./build/orbit-browser-probe-test

.PHONY: test-browser-capture
.PHONY: test-viking-session
build/orbit-viking-session-test: backend/viking_cookies.c backend/viking_session.c backend/https.c backend/viking_session.h backend/browser_services.c backend/browser_match.c backend/providers.c backend/provider_network.c tests/viking_session_driver.c build/sqlite-host.o
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -DORBIT_TEST backend/viking_cookies.c backend/viking_session.c backend/https.c backend/browser_services.c backend/browser_match.c backend/providers.c backend/provider_network.c tests/viking_session_driver.c build/sqlite-host.o -o $@ -lcurl -lssl -lcrypto -lpthread -lm
test-viking-session: build/orbit-viking-session-test
	python3 tests/viking_session_test.py
build/orbit-browser-services-test: backend/browser_services.c backend/ps4_modules.c backend/ps4_modules.h backend/browser_platform.h tests/browser_services_driver.c
	$(HOST_CC) $(CFLAGS) -D_DEFAULT_SOURCE -DORBIT_BROWSER_PROBE -Itests/stubs -Ibackend backend/browser_services.c backend/ps4_modules.c tests/browser_services_driver.c -lpthread -o $@
.PHONY: test-browser-services
build/orbit-browser-worker-test: backend/browser.c $(HEADERS) tests/browser_worker_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -DORBIT_TEST backend/browser.c .deps/cjson/cJSON.c tests/browser_worker_driver.c -Wl,--wrap=pthread_attr_setstacksize -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join -o $@ -lpthread -lm
build/orbit-browser-return-queue-test: backend/browser.c $(HEADERS) tests/browser_return_queue_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DORBIT_TEST backend/browser.c .deps/cjson/cJSON.c tests/browser_return_queue_driver.c -o $@ -lpthread -lm
test-browser-services: build/orbit-browser-services-test build/orbit-browser-worker-test build/orbit-browser-return-queue-test
	python3 -c 'import subprocess; [subprocess.run(["./build/orbit-browser-services-test", str(i)], check=True) for i in range(13)]'
	python3 -c 'import subprocess; [subprocess.run(["./build/orbit-browser-return-queue-test", str(i)], check=True) for i in range(12)]'
	./build/orbit-browser-worker-test idle
	./build/orbit-browser-worker-test request
build/orbit-browser-capture-test: backend/browser_match.c backend/browser_match.h backend/browser_scan.c backend/providers.c backend/provider_network.c tests/browser_capture_driver.c
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DORBIT_DESKTOP -fsanitize=undefined -fsanitize-undefined-trap-on-error backend/browser_match.c backend/browser_scan.c backend/providers.c backend/provider_network.c tests/browser_capture_driver.c -o $@ -lcurl
test-browser-capture: build/orbit-test build/orbit-browser-capture-test
	./build/orbit-browser-capture-test
	python3 tests/browser_capture_integration.py

.PHONY: test-zero-link
test-zero-link: build/orbit-test
	python3 tests/zero_link_integration.py

.PHONY: test-concurrency
test-concurrency: build/orbit-test
	python3 tests/concurrency_integration.py

.PHONY: browser-shortcut
browser-shortcut: build/orbit-browser.pkg
build/orbit-browser.pkg: launcher/browser.c launcher/sce_sys/param.json launcher/sce_sys/icon0.png launcher/tools/build.sh build/generated/network.h app/tools/make_sfo.py app/tools/setup-orbis-toolchain.sh app/tools/setup-orbis-runtime.py app/tooling/orbis-runtime-lock.json app/tooling/orbis-imports.json tools/release-sources.json
	@command -v clang-18 >/dev/null || { echo "Build the browser shortcut in orbit-app-build:0.2 first (bash launcher/tools/build.sh)."; exit 2; }
	bash launcher/tools/build.sh

.PHONY: test-installation-history
test-installation-history: build/orbit-test
	python3 tests/installation_history_integration.py
