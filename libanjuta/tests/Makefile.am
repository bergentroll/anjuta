noinst_PROGRAMS = \
		anjuta-completion-test \
		anjuta-tabber-test \
		anjuta-token-test \
		anjuta-diff-renderer-test

# Include paths
AM_CPPFLAGS = \
        $(WARN_CFLAGS) \
        $(DEPRECATED_FLAGS) \
        $(GDA_CFLAGS) \
        $(LIBANJUTA_CFLAGS)

anjuta_completion_test_CFLAGS = $(LIBANJUTA_CFLAGS)
anjuta_completion_test_LDADD = $(LIBANJUTA_LIBS) $(ANJUTA_LIBS)
anjuta_completion_test_SOURCES = anjuta-completion-test.c

anjuta_tabber_test_CFLAGS = $(LIBANJUTA_CFLAGS)
anjuta_tabber_test_LDADD = $(LIBANJUTA_LIBS) $(ANJUTA_LIBS)

anjuta_tabber_test_SOURCES = anjuta-tabber-test.c


anjuta_token_test_CFLAGS = -g -O0
anjuta_token_test_LDADD = $(ANJUTA_LIBS)

anjuta_token_test_SOURCES = anjuta-token-test.c \
			../anjuta-token.c

anjuta_diff_renderer_test_CFLAGS = $(LIBANJUTA_CFLAGS)
anjuta_diff_renderer_test_LDADD = $(LIBANJUTA_LIBS) $(ANJUTA_LIBS)
anjuta_diff_renderer_test_SOURCES = anjuta-diff-renderer-test.c

CLEANFILES = anjuta_token_test-anjuta-token.gcno \
             anjuta_token_test-anjuta-token-test.gcno \
             anjuta_token_test-anjuta-debug.gcno

-include $(top_srcdir)/git.mk
