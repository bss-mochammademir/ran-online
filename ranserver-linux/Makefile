# THE build gate for the cross-platform foundation.
# `make verify` runs a clean-Linux build inside the dev container — free,
# portable, account-independent (pilar 4 OpEx + A2 cloud-exit). There is NO
# hosted CI by design (minimize vendor lock-in); this local gate is the only one.
# If hosted CI is ever needed it would be a portable cloud-build — not on the
# radar, and never brand-specific (no GitHub Actions).

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
