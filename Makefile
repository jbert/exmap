
# There's probably a better way of doing this (apart from automake),
# would anyone care to enlighten me?

# Note this is the build order, and reflects inter-subdir
# dependencies.
SUBDIRS=kernel jutil src tools

.PHONY: build clean test $(SUBDIRS)

DOSUBDIRS=for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || { exit 1; }; \
	done

build: $(SUBDIRS)
	$(DOSUBDIRS)

clean: $(SUBDIRS)
	$(DOSUBDIRS)

test: build
	make -C src test

# Asciidoc is fairly human-readable, but you can
# use asciidoc to convert the plain-text source docs
# into HTML. This has been tested on asciidoc-8.4.5.
docs: doc.asciidoc FAQ.asciidoc screenshots/screenshot-processes.png screenshots/screenshot-files.png
	asciidoc -b html4 doc.asciidoc
	asciidoc -b html4 FAQ.asciidoc
