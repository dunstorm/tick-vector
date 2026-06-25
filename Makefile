BUILD_DIR ?= build
GENERATOR ?= Ninja
BIN_PATH := $(BUILD_DIR)/tick-vector

.PHONY: configure build run screenshot screenshot-feed-settings screenshot-connection-test screenshot-instrument screenshot-chart screenshot-dom reset-credentials reset-keychain clean

configure:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)"

build: configure
	cmake --build $(BUILD_DIR)

run: build
	@"$(BIN_PATH)"

screenshot: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot /tmp/tick-vector.png
	@echo "Wrote /tmp/tick-vector.png"

screenshot-feed-settings: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-feed-settings /tmp/tick-vector-feed-settings.png
	@echo "Wrote /tmp/tick-vector-feed-settings.png"

screenshot-connection-test: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-connection-test /tmp/tick-vector-connection-test.png
	@echo "Wrote /tmp/tick-vector-connection-test.png"

screenshot-instrument: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-instrument /tmp/tick-vector-instrument.png
	@echo "Wrote /tmp/tick-vector-instrument.png"

screenshot-chart: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-chart /tmp/tick-vector-chart.png
	@echo "Wrote /tmp/tick-vector-chart.png"

screenshot-dom: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-dom /tmp/tick-vector-dom.png
	@echo "Wrote /tmp/tick-vector-dom.png"

reset-credentials:
	@rm -f "$$HOME/Library/Application Support/Tick Vector/feed-connections.json"
	@rm -f "$$HOME/Library/Application Support/Tick Vector/rithmic-profile.json"
	@echo "Removed plaintext Tick Vector credential profiles if they existed."

reset-keychain:
	-@security delete-generic-password -s com.tickvector.desktop.feed-connections -a profiles-v1 >/dev/null 2>&1 || true
	-@security delete-generic-password -s com.tickvector.desktop.rithmic -a profile-v1 >/dev/null 2>&1 || true
	@echo "Removed Tick Vector Keychain profiles if they existed."

clean:
	cmake --build $(BUILD_DIR) --target clean
