import 'package:flutter/material.dart';
import 'package:file_picker/file_picker.dart';
import '../services/settings_manager.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final _settings = SettingsManager.instance;

  // Controllers
  late TextEditingController _extractDirCtrl;
  late TextEditingController _compressDirCtrl;
  late TextEditingController _logDirCtrl;
  late TextEditingController _hunkSizeCtrl;
  late TextEditingController _unitSizeCtrl;
  late TextEditingController _threadsCtrl;
  late TextEditingController _liteCodecListCtrl;

  // Hash algorithm toggles
  bool _sha1 = true;
  bool _md5 = false;
  bool _crc32 = false;
  bool _sha256 = false;
  bool _xxh3 = false;

  // Log output checkboxes
  bool _logRead = true;
  bool _logHash = true;
  bool _logCompress = true;

  // Hash log type checkboxes
  bool _hashLogSvg = false;
  bool _hashLogLog = true;
  bool _hashLogJson = false;

  // Other settings
  bool _splitBin = true;
  String _logLevel = 'info';

  // Compression settings
  String _compCodec = 'best'; // 'best' or 'chdman'
  String _liteCodec = 'auto'; // 'auto' or 'custom'

  @override
  void initState() {
    super.initState();
    _extractDirCtrl = TextEditingController(text: _settings.get('output.extract_output_dir', ''));
    _compressDirCtrl = TextEditingController(text: _settings.get('output.compress_output_dir', ''));
    _logDirCtrl = TextEditingController(text: _settings.get('output.log_output_dir', 'desktop'));
    _hunkSizeCtrl = TextEditingController(text: _settings.get('compress.hunk_size', '65536'));
    _unitSizeCtrl = TextEditingController(text: _settings.get('compress.unit_size', '2048'));
    _threadsCtrl = TextEditingController(text: _settings.get('compress.threads', '0'));
    _liteCodecListCtrl = TextEditingController(text: _settings.get('compress.lite_codec_list', 'zstd,lzma,zlib,huff'));

    final algorithms = _settings.getList('hash.algorithms', ['sha1']);
    _sha1 = algorithms.contains('sha1');
    _md5 = algorithms.contains('md5');
    _crc32 = algorithms.contains('crc32');
    _sha256 = algorithms.contains('sha256');
    _xxh3 = algorithms.contains('xxh3');

    _splitBin = _settings.getBool('extract.split_bin', true);
    _logLevel = _settings.get('app.log_level', 'info');

    _logRead = _settings.getBool('log.read', true);
    _logHash = _settings.getBool('log.hash', true);
    _logCompress = _settings.getBool('log.compress', true);

    _hashLogSvg = _settings.getBool('hash.log_svg', false);
    _hashLogLog = _settings.getBool('hash.log_log', true);
    _hashLogJson = _settings.getBool('hash.log_json', false);

    _compCodec = _settings.get('compress.comp_codec', 'best');
    _liteCodec = _settings.get('compress.lite_codec', 'auto');
  }

  @override
  void dispose() {
    _extractDirCtrl.dispose();
    _compressDirCtrl.dispose();
    _logDirCtrl.dispose();
    _hunkSizeCtrl.dispose();
    _unitSizeCtrl.dispose();
    _threadsCtrl.dispose();
    _liteCodecListCtrl.dispose();
    super.dispose();
  }

  void _save() {
    _settings.set('output.extract_output_dir', _extractDirCtrl.text);
    _settings.set('output.compress_output_dir', _compressDirCtrl.text);
    _settings.set('output.log_output_dir', _logDirCtrl.text);
    _settings.set('compress.hunk_size', _hunkSizeCtrl.text);
    _settings.set('compress.unit_size', _unitSizeCtrl.text);
    _settings.set('compress.threads', _threadsCtrl.text);
    _settings.set('compress.lite_codec_list', _liteCodecListCtrl.text);

    final algorithms = <String>[];
    if (_sha1) algorithms.add('sha1');
    if (_md5) algorithms.add('md5');
    if (_crc32) algorithms.add('crc32');
    if (_sha256) algorithms.add('sha256');
    if (_xxh3) algorithms.add('xxh3');
    _settings.setList('hash.algorithms', algorithms);

    _settings.setBool('extract.split_bin', _splitBin);
    _settings.set('app.log_level', _logLevel);

    _settings.setBool('log.read', _logRead);
    _settings.setBool('log.hash', _logHash);
    _settings.setBool('log.compress', _logCompress);

    _settings.setBool('hash.log_svg', _hashLogSvg);
    _settings.setBool('hash.log_log', _hashLogLog);
    _settings.setBool('hash.log_json', _hashLogJson);

    _settings.set('compress.comp_codec', _compCodec);
    _settings.set('compress.lite_codec', _liteCodec);

    _settings.save();

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Settings saved'), duration: Duration(seconds: 1)),
      );
    }
  }

  Future<void> _pickDirectory(TextEditingController controller) async {
    final selected = await FilePicker.platform.getDirectoryPath();
    if (!mounted || selected == null || selected.isEmpty) return;
    setState(() => controller.text = selected);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
        actions: [
          TextButton.icon(
            onPressed: _save,
            icon: const Icon(Icons.save),
            label: const Text('Save'),
          ),
        ],
      ),
      body: DefaultTabController(
        length: 4,
        child: Column(
          children: [
            const TabBar(
              tabs: [
                Tab(text: 'Output'),
                Tab(text: 'Compression'),
                Tab(text: 'Hash'),
                Tab(text: 'Advanced'),
              ],
            ),
            Expanded(
              child: TabBarView(
                children: [
                  _buildOutputTab(),
                  _buildCompressionTab(),
                  _buildHashTab(),
                  _buildAdvancedTab(),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildOutputTab() {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _buildDirField('Extract Output Directory', _extractDirCtrl, 'Empty = same as input'),
        const SizedBox(height: 16),
        _buildDirField('Compress Output Directory', _compressDirCtrl, 'Empty = same as input'),
        const SizedBox(height: 16),
        _buildDirField('Log Output Directory', _logDirCtrl, '"desktop", "logs", or absolute path'),
        Padding(
          padding: const EdgeInsets.only(left: 12, top: 4),
          child: Text(
            'Outputs read/hash/create/extract results and the error log.',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
              color: Theme.of(context).colorScheme.onSurfaceVariant,
            ),
          ),
        ),
        const SizedBox(height: 16),
        const Text('Log Output', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        const SizedBox(height: 4),
        CheckboxListTile(
          title: const Text('Read'),
          dense: true,
          value: _logRead,
          onChanged: (v) => setState(() => _logRead = v!),
        ),
        CheckboxListTile(
          title: const Text('Hash'),
          dense: true,
          value: _logHash,
          onChanged: (v) => setState(() => _logHash = v!),
        ),
        CheckboxListTile(
          title: const Text('Create / Extract'),
          dense: true,
          value: _logCompress,
          onChanged: (v) => setState(() => _logCompress = v!),
        ),
        const SizedBox(height: 16),
        SwitchListTile(
          title: const Text('Split BIN files'),
          subtitle: const Text('Extract to per-track BIN files'),
          value: _splitBin,
          onChanged: (v) => setState(() => _splitBin = v),
        ),
      ],
    );
  }

  Widget _buildCompressionTab() {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Text('Comp (Max Compression)', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        RadioListTile<String>(
          title: const Text('Max compression (--best)'),
          value: 'best',
          groupValue: _compCodec,
          onChanged: (v) => setState(() => _compCodec = v!),
        ),
        RadioListTile<String>(
          title: const Text('chdman-compatible defaults (--chdman)'),
          value: 'chdman',
          groupValue: _compCodec,
          onChanged: (v) => setState(() => _compCodec = v!),
        ),
        const Divider(height: 32),
        const Text('Lite (Fast)', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        RadioListTile<String>(
          title: const Text('Auto (detect media/platform and choose codecs)'),
          value: 'auto',
          groupValue: _liteCodec,
          onChanged: (v) => setState(() => _liteCodec = v!),
        ),
        RadioListTile<String>(
          title: const Text('Custom codec list'),
          value: 'custom',
          groupValue: _liteCodec,
          onChanged: (v) => setState(() => _liteCodec = v!),
        ),
        if (_liteCodec == 'custom') ...[
          const SizedBox(height: 8),
          TextField(
            controller: _liteCodecListCtrl,
            decoration: const InputDecoration(
              labelText: 'Codec list',
              helperText: 'Comma-separated, up to 4: zstd,lzma,zlib,huffman,flac',
              border: OutlineInputBorder(),
            ),
          ),
        ],
        const Divider(height: 32),
        TextField(
          controller: _hunkSizeCtrl,
          decoration: const InputDecoration(
            labelText: 'Hunk Size (bytes)',
            helperText: 'Default: 65536',
            border: OutlineInputBorder(),
          ),
          keyboardType: TextInputType.number,
        ),
        const SizedBox(height: 16),
        TextField(
          controller: _unitSizeCtrl,
          decoration: const InputDecoration(
            labelText: 'Unit Size (bytes)',
            helperText: 'Default: 2048',
            border: OutlineInputBorder(),
          ),
          keyboardType: TextInputType.number,
        ),
        const SizedBox(height: 16),
        TextField(
          controller: _threadsCtrl,
          decoration: const InputDecoration(
            labelText: 'Threads',
            helperText: '0 = auto-detect',
            border: OutlineInputBorder(),
          ),
          keyboardType: TextInputType.number,
        ),
      ],
    );
  }

  Widget _buildHashTab() {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Text('Hash Algorithms', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        CheckboxListTile(title: const Text('SHA-1'), value: _sha1, onChanged: (v) => setState(() => _sha1 = v!)),
        CheckboxListTile(title: const Text('MD5'), value: _md5, onChanged: (v) => setState(() => _md5 = v!)),
        CheckboxListTile(title: const Text('CRC32'), value: _crc32, onChanged: (v) => setState(() => _crc32 = v!)),
        CheckboxListTile(title: const Text('SHA-256'), value: _sha256, onChanged: (v) => setState(() => _sha256 = v!)),
        CheckboxListTile(title: const Text('XXH3'), value: _xxh3, onChanged: (v) => setState(() => _xxh3 = v!)),
        const Divider(height: 32),
        const Text('Log Type', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        CheckboxListTile(title: const Text('SVG'), value: _hashLogSvg, onChanged: (v) => setState(() => _hashLogSvg = v!)),
        CheckboxListTile(title: const Text('Log (plain text)'), value: _hashLogLog, onChanged: (v) => setState(() => _hashLogLog = v!)),
        CheckboxListTile(title: const Text('JSON'), value: _hashLogJson, onChanged: (v) => setState(() => _hashLogJson = v!)),
      ],
    );
  }

  Widget _buildAdvancedTab() {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        DropdownButtonFormField<String>(
          initialValue: _logLevel,
          decoration: const InputDecoration(
            labelText: 'Log Level',
            border: OutlineInputBorder(),
          ),
          items: const [
            DropdownMenuItem(value: 'debug', child: Text('Debug')),
            DropdownMenuItem(value: 'info', child: Text('Info')),
            DropdownMenuItem(value: 'warning', child: Text('Warning')),
            DropdownMenuItem(value: 'error', child: Text('Error')),
            DropdownMenuItem(value: 'critical', child: Text('Critical')),
            DropdownMenuItem(value: 'none', child: Text('None')),
          ],
          onChanged: (v) => setState(() => _logLevel = v!),
        ),
        const SizedBox(height: 24),
        const Text('App Mode', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        ListTile(
          title: const Text('Portable (default)'),
          subtitle: const Text('Config stored next to executable'),
          leading: const Icon(Icons.folder_special),
        ),
        const SizedBox(height: 8),
        FilledButton.tonalIcon(
          onPressed: () {
            // TODO: Install mode — copy to system paths
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('Install mode not yet implemented'), duration: Duration(seconds: 1)),
            );
          },
          icon: const Icon(Icons.install_desktop),
          label: const Text('Switch to Install Mode'),
        ),
      ],
    );
  }

  Widget _buildDirField(String label, TextEditingController ctrl, String helper) {
    return TextField(
      controller: ctrl,
      decoration: InputDecoration(
        labelText: label,
        helperText: helper,
        border: const OutlineInputBorder(),
        suffixIcon: IconButton(
          icon: const Icon(Icons.folder_open),
          onPressed: () async {
            await _pickDirectory(ctrl);
          },
        ),
      ),
    );
  }
}
