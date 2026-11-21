#pragma once

#ifdef __WXGTK__
#include <gio/gio.h>
#include <filesystem>
#include <fstream>

// A real private-bus FileManager1 peer records the public ShowItems boundary.
inline void RunOwnedFileManager(std::filesystem::path directory) {
    constexpr const char* xml =
        "<node><interface name='org.freedesktop.FileManager1'><method name='ShowItems'>"
        "<arg type='as' direction='in'/><arg type='s' direction='in'/></method></interface></node>";
    auto* information = g_dbus_node_info_new_for_xml(xml, nullptr);
    auto* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!information || !connection) std::exit(2);
    const GDBusInterfaceVTable table{
        +[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters,
            GDBusMethodInvocation* invocation, gpointer data) {
            const auto& root = *static_cast<const std::filesystem::path*>(data);
            if (std::filesystem::exists(root / "filemanager-reject")) {
                g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.NotSupported",
                                                           "Owned manager declines item selection");
                return;
            }
            gchar** items = nullptr;
            const gchar* startup = nullptr;
            g_variant_get(parameters, "(^as&s)", &items, &startup);
            {
                std::ofstream received(root / "filemanager-received.tmp", std::ios::binary);
                for (size_t index = 0; items[index]; ++index)
                    received << items[index] << '\n';
            }
            g_strfreev(items);
            std::filesystem::rename(root / "filemanager-received.tmp", root / "filemanager-received");
            g_dbus_method_invocation_return_value(invocation, nullptr);
        },
        nullptr,
        nullptr,
        {nullptr}};
    const auto registered = g_dbus_connection_register_object(
        connection, "/org/freedesktop/FileManager1", information->interfaces[0], &table, &directory, nullptr, nullptr);
    if (!registered) std::exit(3);
    const auto owner = g_bus_own_name_on_connection(
        connection, "org.freedesktop.FileManager1", G_BUS_NAME_OWNER_FLAGS_NONE,
        +[](GDBusConnection*, const gchar*, gpointer data) {
            std::ofstream(*static_cast<const std::filesystem::path*>(data) / "filemanager-ready") << "ready";
        },
        nullptr, &directory, nullptr);
    auto* loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_bus_unown_name(owner);
    g_dbus_connection_unregister_object(connection, registered);
    g_object_unref(connection);
    g_dbus_node_info_unref(information);
}
#endif
