import 'dart:io';
import 'package:path_provider/path_provider.dart';

class SettingsManager {
  static final SettingsManager instance = SettingsManager._();
  SettingsManager._();

  final Map<String, dynamic> _data = {};
  String? _configPath;

  /// Load settings from TOML file (or initialize defaults)
  Future<void> load() async {
    _configPath = await _resolveConfigPath();

    final file = File(_configPath!);
    if (file.existsSync()) {
      _parseSimpleConfig(file.readAsStringSync());
    } else {
      // Set defaults
      _data['app.mode'] = 'install';
      _data['app.log_level'] = 'info';
      _data['output.extract_output_dir'] = '';
      _data['output.compress_output_dir'] = '';
      _data['output.hash_output_dir'] = 'desktop';
      _data['hash.algorithms'] = ['sha1'];
      _data['hash.output_format'] = 'text';
      _data['compress.codec'] = 'zstd';
      _data['compress.hunk_size'] = '65536';
      _data['compress.unit_size'] = '2048';
      _data['compress.threads'] = '0';
      _data['extract.split_bin'] = false;
      _data['result.format'] = 'text';
    }
  }

  /// Get a string setting
  String get(String key, String defaultValue) {
    final v = _data[key];
    if (v == null) return defaultValue;
    return v.toString();
  }

  /// Get a bool setting
  bool getBool(String key, bool defaultValue) {
    final v = _data[key];
    if (v == null) return defaultValue;
    if (v is bool) return v;
    return v.toString().toLowerCase() == 'true';
  }

  /// Get a list setting
  List<String> getList(String key, List<String> defaultValue) {
    final v = _data[key];
    if (v == null) return defaultValue;
    if (v is List) return v.cast<String>();
    return defaultValue;
  }

  /// Set a string setting
  void set(String key, String value) {
    _data[key] = value;
  }

  /// Set a bool setting
  void setBool(String key, bool value) {
    _data[key] = value;
  }

  /// Set a list setting
  void setList(String key, List<String> value) {
    _data[key] = value;
  }

  /// Save settings to config file
  Future<void> save() async {
    if (_configPath == null) return;

    final buffer = StringBuffer();
    buffer.writeln('# CHDlite GUI Settings');
    buffer.writeln('# Auto-generated — edit with care');
    buffer.writeln();

    // Group by section
    final sections = <String, Map<String, dynamic>>{};
    for (final entry in _data.entries) {
      final dot = entry.key.indexOf('.');
      if (dot > 0) {
        final section = entry.key.substring(0, dot);
        final field = entry.key.substring(dot + 1);
        sections.putIfAbsent(section, () => {});
        sections[section]![field] = entry.value;
      }
    }

    for (final section in sections.entries) {
      buffer.writeln('[${section.key}]');
      for (final field in section.value.entries) {
        final v = field.value;
        if (v is bool) {
          buffer.writeln('${field.key} = $v');
        } else if (v is List) {
          final items = v.map((e) => '"$e"').join(', ');
          buffer.writeln('${field.key} = [$items]');
        } else {
          buffer.writeln('${field.key} = "${v.toString()}"');
        }
      }
      buffer.writeln();
    }

    final file = File(_configPath!);
    await file.parent.create(recursive: true);
    await file.writeAsString(buffer.toString());
  }

  /// Simple TOML-like parser (handles our flat key-value format)
  void _parseSimpleConfig(String content) {
    String currentSection = '';
    for (var line in content.split('\n')) {
      line = line.trim();
      if (line.isEmpty || line.startsWith('#')) continue;

      if (line.startsWith('[') && line.endsWith(']')) {
        currentSection = line.substring(1, line.length - 1);
        continue;
      }

      final eq = line.indexOf('=');
      if (eq < 0) continue;

      final key = '$currentSection.${line.substring(0, eq).trim()}';
      var value = line.substring(eq + 1).trim();

      // Parse value type
      if (value == 'true') {
        _data[key] = true;
      } else if (value == 'false') {
        _data[key] = false;
      } else if (value.startsWith('[') && value.endsWith(']')) {
        // Array
        final inner = value.substring(1, value.length - 1);
        _data[key] = inner
            .split(',')
            .map((s) => s.trim().replaceAll('"', ''))
            .where((s) => s.isNotEmpty)
            .toList();
      } else {
        // String — strip quotes
        if ((value.startsWith('"') && value.endsWith('"')) ||
            (value.startsWith("'") && value.endsWith("'"))) {
          value = value.substring(1, value.length - 1);
        }
        _data[key] = value;
      }
    }
  }

  /// Resolve config file path based on platform
  Future<String> _resolveConfigPath() async {
    // Check for portable mode: config next to executable
    final exeDir = File(Platform.resolvedExecutable).parent.path;
    final portableConfig = '$exeDir/chdlite.toml';
    if (File(portableConfig).existsSync()) {
      _data['app.mode'] = 'portable';
      return portableConfig;
    }

    // Install mode: platform-standard location
    if (Platform.isMacOS) {
      final appSupport = await getApplicationSupportDirectory();
      return '${appSupport.path}/chdlite.toml';
    } else if (Platform.isWindows) {
      final appData = Platform.environment['APPDATA'];
      if (appData != null) {
        return '$appData/chdlite/chdlite.toml';
      }
    } else if (Platform.isLinux) {
      final configHome = Platform.environment['XDG_CONFIG_HOME'] ??
          '${Platform.environment['HOME']}/.config';
      return '$configHome/chdlite/chdlite.toml';
    }

    // Fallback
    final appSupport = await getApplicationSupportDirectory();
    return '${appSupport.path}/chdlite.toml';
  }
}
