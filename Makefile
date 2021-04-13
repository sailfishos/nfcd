# -*- Mode: makefile-gmake -*-
.PHONY: all clean install test

all:
	@$(MAKE) -C src $@

test:
	@$(MAKE) -C unit $@

pkgconfig:
	@$(MAKE) -C core $@

install:
	@$(MAKE) -C src $@
	@$(MAKE) -C core $@
	@$(MAKE) -C tools $@
	@$(MAKE) -C plugins $@

clean:
	@$(MAKE) -C src $@
	@$(MAKE) -C core $@
	@$(MAKE) -C tools $@
	@$(MAKE) -C plugins $@
	@$(MAKE) -C unit $@
	rm -f *~ rpm/*~

.DEFAULT:
	@$(MAKE) -C src $@
	@$(MAKE) -C tools $@

debian/%.install: debian/%.install.in
	sed 's|@LIBDIR@|$(LIBDIR)|g' $< > $@
