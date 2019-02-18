# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C core_adapter $*
	@$(MAKE) -C core_crc $*
	@$(MAKE) -C core_manager $*
	@$(MAKE) -C core_ndef_rec $*
	@$(MAKE) -C core_ndef_rec_u $*
	@$(MAKE) -C core_ndef_rec_t $*
	@$(MAKE) -C core_plugin $*
	@$(MAKE) -C core_plugins $*
	@$(MAKE) -C core_tag $*
	@$(MAKE) -C core_tag_t2 $*
	@$(MAKE) -C core_target $*
	@$(MAKE) -C core_tlv $*
	@$(MAKE) -C core_util $*
	@$(MAKE) -C plugins_dbus_handlers_config $*
	@$(MAKE) -C plugins_dbus_handlers_type_mediatype $*
	@$(MAKE) -C plugins_dbus_handlers_type_uri $*

clean: unitclean
	rm -f *~
	rm -f common/*~
	rm -f coverage/*~
	rm -f coverage/*.gcov
	rm -fr coverage/report
