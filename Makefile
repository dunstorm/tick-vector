BUILD_DIR ?= build
GENERATOR ?= Ninja
BIN_PATH := $(BUILD_DIR)/trading-client

.PHONY: configure build run screenshot screenshot-feed-settings screenshot-connection-test screenshot-instrument screenshot-chart reset-credentials reset-keychain clean

configure:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)"

build: configure
	cmake --build $(BUILD_DIR)

run: build
	@"$(BIN_PATH)"

screenshot: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot /tmp/trading-client.png
	@echo "Wrote /tmp/trading-client.png"

screenshot-feed-settings: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-feed-settings /tmp/trading-client-feed-settings.png
	@echo "Wrote /tmp/trading-client-feed-settings.png"

screenshot-connection-test: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-connection-test /tmp/trading-client-connection-test.png
	@echo "Wrote /tmp/trading-client-connection-test.png"

screenshot-instrument: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-instrument /tmp/trading-client-instrument.png
	@echo "Wrote /tmp/trading-client-instrument.png"

screenshot-chart: build
	@QT_QPA_PLATFORM=offscreen "$(BIN_PATH)" --screenshot-chart /tmp/trading-client-chart.png
	@echo "Wrote /tmp/trading-client-chart.png"

reset-credentials:
	@rm -f "$$HOME/Library/Application Support/Trading Client/feed-connections.json"
	@rm -f "$$HOME/Library/Application Support/Trading Client/rithmic-profile.json"
	@echo "Removed plaintext Trading Client credential profiles if they existed."

reset-keychain:
	-@security delete-generic-password -s com.tradingclient.desktop.feed-connections -a profiles-v1 >/dev/null 2>&1 || true
	-@security delete-generic-password -s com.tradingclient.desktop.rithmic -a profile-v1 >/dev/null 2>&1 || true
	@echo "Removed Trading Client Keychain profiles if they existed."

clean:
	cmake --build $(BUILD_DIR) --target clean
