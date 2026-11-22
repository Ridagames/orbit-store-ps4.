# Orbit Store TV app - build entry points (run inside the app build image).
# Adapted from ps5-homebrew-ui / ps5-native-app-boilerplate (GPL-3.0-or-later).
#
#   docker build -t orbit-app-build:0.2 app
#   docker run --rm -v "$PWD:/work" -w /work/app orbit-app-build:0.2 make pkg
#
# The console build uses the pinned OpenOrbis FreeBSD toolchain: vanilla
# clang targets x86_64-pc-freebsd12-elf, ld.lld links the OELF with link.x,
# create-fself fake-signs it, and LibOrbisPkg wraps it into a ORBT00002.pkg.

SHELL := /bin/bash
.DEFAULT_GOAL := app

# Extra build inputs forwarded to tools/build.sh.
APP_DEFINITIONS ?=
APP_INCLUDE_PATHS ?=
BUILD_JOBS ?= $(shell nproc 2>/dev/null || echo 2)
USE_CCACHE ?= 0
export BUILD_JOBS USE_CCACHE
export APP_DEFINITIONS APP_INCLUDE_PATHS

# Orbit itself, carried in the package so the app can start it when it isn't
# running (src/orbit/starter.hpp). Build it first with `make payload` in the
# repository root, or pass ORBIT_PAYLOAD=none for an app that only waits.
ORBIT_PAYLOAD ?= ../build/orbit_store.elf
PAYLOAD_STAGE := build/package-extra
export APP_EXTRA_DIR := $(PAYLOAD_STAGE)

.PHONY: app pkg deps payload-stage preview test test-backdrop fonts clean distclean help shader-capture

shader-capture:
	@bash tools/build-shader-capture.sh

app: payload-stage
	@printf '%s\n' '==> [app] Compiling, linking, signing and assembling the app folder'
	@bash tools/build.sh Folder

pkg: payload-stage
	@printf '%s\n' '==> [pkg] Building the app folder and its installable package'
	@bash tools/build.sh Pkg

deps:
	@bash tools/setup-test-dependencies.sh >/dev/null
	@bash tools/setup-orbis-toolchain.sh
	@bash tools/setup-webp-dependencies.sh

payload-stage:
	@bash tools/stage-payload.sh "$(ORBIT_PAYLOAD)" "$(PAYLOAD_STAGE)"

# Pictures of every screen, rendered on this machine (build/preview/pictures).
preview:
	@bash tools/preview.sh build/preview/pictures all

test:
	@bash tools/build-tests.sh
	@ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		build/tests/unit_tests --gtest_brief=1

# Real GLES2 rendering catches shader precision failures that UI unit tests cannot.
test-backdrop:
	@mkdir -p build/tests
	@clang++ -std=c++20 -O2 -Wall -Wextra -Werror -DGL_GLEXT_PROTOTYPES=1 -Isrc \
		-ffunction-sections -fdata-sections tests/gfx/backdrop_test.cpp \
		src/gfx/backdrop.cpp src/gfx/gl_program.cpp src/gfx/gl_batch.cpp src/gfx/draw_list.cpp \
		host/platform_host.cpp -Wl,--gc-sections -lEGL -lGLESv2 -o build/tests/backdrop_test
	@EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
		build/tests/backdrop_test

fonts:
	@bash tools/bake-fonts.sh

clean:
	@rm -rf -- build dist

distclean: clean
	@rm -rf -- .deps

help:
	@printf '%s\n' \
	  'make            Build the PS4 app folder in dist/ORBT00002' \
	  'make pkg        Also build the installable .pkg in dist/' \
	  'make preview    Render every screen on this machine to PNG' \
	  'make test       Host unit tests (sanitizers on)' \
	  'make fonts      Rebake the Inter SDF fonts'
