
bash_completion_source = grub-completion.bash.in
bash_completion_script = grub

EXTRA_DIST = $(bash_completion_source)

CLEANFILES = $(bash_completion_script) config.log

bashcompletiondir = $(sysconfdir)/bash_completion.d
bashcompletion_DATA = $(bash_completion_script)

$(bash_completion_script): $(bash_completion_source) $(top_builddir)/config.status
	$(top_builddir)/config.status --file=$@:$<
