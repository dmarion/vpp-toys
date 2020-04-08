VPP_DIR?=$(PWD)/../vpp

build_dir = $(PWD)/build

define configure
	@mkdir -p $(build_dir)
	@cd $(build_dir) && cmake -G Ninja \
	  -DVPP_PATH:PATH=$(VPP_DIR) \
	  $(PWD)
endef

$(build_dir):
	$(call configure)


.PHONY: build
build: $(build_dir)
	@cmake --build $<

clean:
	@rm -rf $(build_dir) bin

fixstyle:
	@for i in */*.[ch]; do indent $$i; done

.PHONY: ctags
ctags:
	@find . $(VPP_DIR) -name \*.[chS] > ctags.files
	@ctags --totals --tag-relative -L ctags.files
	@rm ctags.files

.DEFAULT_GOAL := help
help:
	@echo "Make Targets:"
	@echo " build                - build binaries"
	@echo " clean                - wipe all build products"
	@echo " ctags                - (re)generate ctags database"
	@echo ""
	@echo "Make Arguments:"
	@echo " VPP_DIR=<path>       - path to VPP directory"

