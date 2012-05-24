AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = -I m4

if WQ_ENABLE_DOC
DOC_DIR = doc
else
DOC_DIR =
endif

if WQ_ENABLE_EXAMPLES
EXAMPLES_DIR = examples
else
EXAMPLES_DIR =
endif

SUBDIRS = src $(EXAMPLES_DIR) $(DOC_DIR)

dist_noinst_SCRIPTS = autogen.sh
