.PHONY: install clean clean-full runtime runtime-android compile-flags \
	gen gen-docs gen-types docs-diagrams docs-diagrams-png relink \
	publish update-protocol

install:
	$(call run,./bin/install.sh $(INSTALL_ARGS))

clean:
	$(call run,./bin/clean.sh $(CLEAN_ARGS))

clean-full:
	$(call run,./bin/clean.sh --full $(CLEAN_ARGS))

runtime:
	$(call run,./bin/build-runtime-library.sh $(RUNTIME_ARGS))

runtime-android:
	$(call run,./bin/build-runtime-library.sh --platform android $(RUNTIME_ANDROID_ARGS))

compile-flags:
	$(call run,./bin/generate-compile-flags-txt.sh $(COMPILE_FLAGS_ARGS))

gen:
	$(call run,npm run gen)

gen-docs:
	$(call run,npm run gen:docs)

gen-types:
	$(call run,npm run gen:tsc)

docs-diagrams:
	$(call run,npm run docs:diagrams)

docs-diagrams-png:
	$(call run,npm run docs:diagrams:png)

relink:
	$(call run,npm run relink)

publish:
	$(call run,./bin/publish-npm-modules.sh $(PUBLISH_ARGS))

update-protocol:
	$(call run,npm run update-network-protocol)
