# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C core_adapter $*
	@$(MAKE) -C core_config $*
	@$(MAKE) -C core_crc $*
	@$(MAKE) -C core_initiator $*
	@$(MAKE) -C core_llc $*
	@$(MAKE) -C core_llc_param $*
	@$(MAKE) -C core_manager $*
	@$(MAKE) -C core_ndef_rec $*
	@$(MAKE) -C core_ndef_rec_sp $*
	@$(MAKE) -C core_ndef_rec_t $*
	@$(MAKE) -C core_ndef_rec_u $*
	@$(MAKE) -C core_peer $*
	@$(MAKE) -C core_peer_service $*
	@$(MAKE) -C core_peer_services $*
	@$(MAKE) -C core_peer_socket $*
	@$(MAKE) -C core_plugin $*
	@$(MAKE) -C core_plugins $*
	@$(MAKE) -C core_snep $*
	@$(MAKE) -C core_tag $*
	@$(MAKE) -C core_tag_t2 $*
	@$(MAKE) -C core_tag_t4 $*
	@$(MAKE) -C core_target $*
	@$(MAKE) -C core_util $*
	@$(MAKE) -C plugins_dbus_handlers $*
	@$(MAKE) -C plugins_dbus_handlers_config $*
	@$(MAKE) -C plugins_dbus_handlers_type_generic $*
	@$(MAKE) -C plugins_dbus_handlers_type_mediatype $*
	@$(MAKE) -C plugins_dbus_handlers_type_sp $*
	@$(MAKE) -C plugins_dbus_handlers_type_text $*
	@$(MAKE) -C plugins_dbus_handlers_type_uri $*
	@$(MAKE) -C plugins_dbus_service_adapter $*
	@$(MAKE) -C plugins_dbus_service_isodep $*
	@$(MAKE) -C plugins_dbus_service_ndef $*
	@$(MAKE) -C plugins_dbus_service_peer $*
	@$(MAKE) -C plugins_dbus_service_plugin $*
	@$(MAKE) -C plugins_dbus_service_tag $*
	@$(MAKE) -C plugins_dbus_service_tag_t2 $*
	@$(MAKE) -C plugins_dbus_service_util $*
	@$(MAKE) -C plugins_settings $*

clean: unitclean
	rm -f *~
	rm -f common/*~
	rm -f coverage/*~
	rm -f coverage/*.gcov
	rm -fr coverage/report
