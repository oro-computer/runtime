.PHONY: test test-runtime-core test-lint test-ios-simulator test-android test-android-emulator

test:
	$(call run,npm test)

test-runtime-core:
	$(call run,npm run test:runtime-core)

test-lint:
	$(call run,npm run test:lint)

test-ios-simulator:
	$(call run,npm run test:ios-simulator)

test-android:
	$(call run,npm run test:android)

test-android-emulator:
	$(call run,npm run test:android-emulator)
