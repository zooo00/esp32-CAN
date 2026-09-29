# Short targets around idf.py. Each project builds in its own dir with its own sdkconfig.
# Usage: make a PORT=/dev/cu.usbmodem1101

ROOT := $(CURDIR)

# $(1)=project dir
idf = idf.py -C $(ROOT)/$(1) -B $(ROOT)/$(1)/build \
	-D SDKCONFIG=$(ROOT)/$(1)/build/sdkconfig

.PHONY: a b build clean check-idf check-port

check-idf:
	@command -v idf.py >/dev/null || { echo "idf.py not found: run '. ~/esp/esp-idf/export.sh' first"; exit 1; }

# PORT is required: with both boards plugged in, auto-detect can flash the wrong one.
check-port:
	@test -n "$(PORT)" || { echo "Set PORT=/dev/cu.usbmodemXXXX (see: ls /dev/cu.usbmodem*)"; exit 1; }

a: check-idf check-port ## Node A (bike + desk; type "stub on" in the monitor for fake frames)
	$(call idf,node_a) -p $(PORT) build flash monitor

b: check-idf check-port ## Node B: BLE -> USB serial
	$(call idf,node_b) -p $(PORT) build flash monitor

build: check-idf ## Compile both, flash nothing
	$(call idf,node_a) build
	$(call idf,node_b) build

clean:
	rm -rf node_a/build* node_b/build*
