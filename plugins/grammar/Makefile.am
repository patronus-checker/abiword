
SUBDIRS = patronuswrap xp

plugindir = $(ABIWORD_PLUGINSDIR)
plugin_LTLIBRARIES = grammar.la

grammar_la_LIBADD = \
	patronuswrap/libpatronuswrap.la \
	xp/libxp.la \
	$(GRAMMAR_LIBS)

grammar_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined

nodist_EXTRA_grammar_la_SOURCES = dummy.cpp
grammar_la_SOURCES = 

EXTRA_DIST = \
	plugin.m4
