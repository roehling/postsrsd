all install test: _build/Makefile
	$(MAKE) -C _build $@

clean distclean:
	rm -rf _build

_build/Makefile: CMakeLists.txt
	mkdir -p _build
	cd _build && cmake .. -DCMAKE_BUILD_TYPE=Release $(addprefix -DINIT_FLAVOR=,$(INIT_FLAVOR)) $(if $(CFLAGS),-DCMAKE_C_FLAGS="$(CFLAGS)",) $(addprefix -DCMAKE_C_COMPILER=,$(CC)) $(addprefix -DCMAKE_INSTALL_PREFIX=,$(PREFIX))

