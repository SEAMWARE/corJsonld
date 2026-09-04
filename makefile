#
# FILE            makefile
#
# AUTHOR          Ken Zangelin
#
# Copyright 2026 Seamware
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LIB_SO        = libcorJsonld.so
LIB           = libcorJsonld.a
CC            = gcc
INCLUDE       = -I..
DFLAGS        =
#
# EXTRA_CFLAGS - the hook for a caller that needs to ADD flags to this build.
#
# Not DFLAGS. DFLAGS is a plain variable, so `make DFLAGS=...` REPLACES it -
# the command line beats the makefile - and a `DFLAGS +=` inside the makefile is
# ignored along with it, because += never appends to a command-line variable. A
# caller reaching for DFLAGS to add one flag therefore drops every default this
# lib sets for itself. DFLAGS is empty here today, so nothing is lost yet; the
# first -D added to it would be, silently. corNgsild lost -DANSI and
# -DCOR_WITH_ICU that way and compiled the wrong collation path under coverage.
#
# EXTRA_CFLAGS is appended LAST, so a caller's -O0 / -Wno-error also win over the
# -O2 / -Werror here, which is what an instrumented build needs.
#
CFLAGS        = -O2 -Wall -Werror -fPIC -Wno-unused-function -fstack-protector-all $(DFLAGS) $(INCLUDE) -MMD -MP $(EXTRA_CFLAGS)
LIB_SOURCES   = corJsonld.c          \
                corLdInit.c          \
                corLdContextParse.c  \
                corLdExpand.c        \
                corLdCompact.c       \
                corLdExpandTree.c    \
                corLdCompactTree.c   \
                corLdPrefixExpand.c  \
                corLdCache.c         \
                corLdDownload.c      \
                corLdIdGen.c         \
                corLdUrlResolve.c

#
# BUILD - which flavour of build this is, and where its objects live.
#
# Objects used to sit next to their sources, one set for every flavour, and that
# is a silent-wrong-answer machine: `make coverage` leaves instrumented objects
# behind, a later ordinary build finds them NEWER than the sources and relinks
# them into a binary that calls itself ordinary. Nothing warns. One directory
# per flavour means the two cannot be confused.
#
# A plain variable and not a target-specific one on purpose: target-specific
# variables (`debug: CFLAGS += -g`) are not visible when the makefile is parsed,
# so a directory derived from them would be the same directory for every target.
#
BUILD        ?= debug
OBJDIR       := obj/$(BUILD)

ifeq ($(BUILD),debug)
CFLAGS       += -g -DDEBUG
endif

LIB_OBJS      = $(addprefix $(OBJDIR)/,$(LIB_SOURCES:.c=.o))
LIB_DEPS      = $(addprefix $(OBJDIR)/,$(LIB_SOURCES:.c=.d))

#
# $(OBJDIR)/.flags - the flags these objects were built with.
#
# The directory separates the flavours; this catches a change WITHIN one. A
# caller adding EXTRA_CFLAGS changes the compile line and nothing else: sources
# are untouched, objects stay newer than them, and make rebuilds nothing. The
# stamp is rewritten only when the flags actually differ, so its timestamp moves
# exactly when a rebuild is due, and every object depends on it.
#
FLAGSTAMP    := $(OBJDIR)/.flags

SO_LDFLAGS    = -L../kalloc -L../kjson -L../kbase -L../klog -L../ktrace -L../khash
SO_LIBS       = -lkalloc -lkjson -lkbase -lklog -lktrace -lkhash -lpthread
SO_RPATH      = -Wl,-rpath,'$$ORIGIN/../kalloc:$$ORIGIN/../kjson:$$ORIGIN/../kbase:$$ORIGIN/../klog:$$ORIGIN/../ktrace:$$ORIGIN/../khash'

LIBS          = ../kalloc/libkalloc.a ../kjson/libkjson.a ../kbase/libkbase.a ../klog/libklog.a ../ktrace/libktrace.a ../khash/libkhash.a -lpthread

#
# The artefacts are built per flavour and then STAGED to the repo root, where
# every consumer expects them. Unconditionally: the root copy is the last build
# whatever it was, and a stale one cannot survive a flavour switch. Comparing
# timestamps here would reintroduce the very bug the object directories fix -
# obj/debug/libX.a can easily be older than a libX.a left behind by a coverage
# build, and `cp` would then be skipped.
#
all: $(OBJDIR)/$(LIB_SO) $(OBJDIR)/$(LIB)
						@cp -f $(OBJDIR)/$(LIB) $(LIB)
						@cp -f $(OBJDIR)/$(LIB_SO) $(LIB_SO)

$(FLAGSTAMP): FORCE
						@mkdir -p $(OBJDIR)
						@echo '$(CFLAGS)' | cmp -s - $@ 2>/dev/null || echo '$(CFLAGS)' > $@

FORCE:

clean:
						rm -rf obj
						#
						# ...and the legacy in-tree artefacts. Objects live under obj/ now, but a tree
						# built before that still has .o/.d beside its sources - and, worse, .gcno:
						# gcovr reads those and reports a file nobody compiled as entirely unexecuted,
						# which once moved the published figure by three points.
						#
						rm -f *.o *.d *.gcno *.gcda
						rm -f *.o
						rm -f *.a
						rm -f *~
						rm -f *.so

install:    all

di:         install

ci:         clean install

#
# The staged artefacts are targets in their own right, so a caller can ask for
# `make libcorX.a` and get the current flavour's archive copied into place. The
# coverage target does exactly that, by name.
#
$(LIB): $(OBJDIR)/$(LIB)
						@cp -f $< $@

$(LIB_SO): $(OBJDIR)/$(LIB_SO)
						@cp -f $< $@

$(OBJDIR)/$(LIB):	$(LIB_OBJS)
						ar r $@ $(LIB_OBJS)
						ranlib $@

$(OBJDIR)/$(LIB_SO):	$(LIB_OBJS)
						$(CC) -shared $(LIB_OBJS) -o $@ $(SO_LDFLAGS) $(SO_LIBS) $(SO_RPATH)


$(OBJDIR)/%.o: %.c $(FLAGSTAMP)
						@mkdir -p $(OBJDIR)
						$(CC) $(CFLAGS) -c $< -o $@

%.i: %.c
						$(CC) $(CFLAGS) -c $^ -E > $@

-include $(LIB_DEPS)
