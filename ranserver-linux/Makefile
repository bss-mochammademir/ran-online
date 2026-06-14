# Primary build gate for the cross-platform foundation.
# `make verify` runs a clean-Linux build inside the dev container — free,
# portable, account-independent (pilar 4 OpEx + A2 cloud-exit). Hosted CI
# (GitHub Actions) is optional/swappable; this is the canonical gate.

.PHONY: verify build run clean

# Clean-Linux verification via the dev container (the gate).
verify:
	docker build -f Dockerfile.dev -t ran-dev .
	docker run --rm -v "$(CURDIR)":/work ran-dev \
	  bash -lc "cmake -B build -S . && cmake --build build && ./build/foundation_smoke"

# Native local build (needs cmake/g++/libboost-dev/unixodbc-dev on PATH).
build:
	cmake -B build -S . && cmake --build build

run: build
	./build/foundation_smoke

clean:
	rm -rf build build-host
