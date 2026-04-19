import 'package:flutter/material.dart';
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
  late TextEditingController _hashDirCtrl;
  late TextEditingController _codecCtrl;
  late TextEditingController _hunkSizeCtrl;
  late TextEditingController _unitSizeCtrl;
  late TextEditingController _threadsCtrl;

  // Hash algorithm toggles
  bool _sha1 = true;
  bool _md5 = false;
  bool _crc32 = false;
  bool _sha256 = false;
  bool _xxh3 = false;

  // Other settings
  bool _splitBin = false;
  String _logLevel = 'info';

  @override
  void initState() {
    super.initState();
    _extractDirCtrl = TextEditingController(text: _settings.get('output.extract_output_dir', ''));
    _compressDirCtrl = TextEditingController(text: _settings.get('output.compress_output_dir', ''));
    _hashDirCtrl = TextEditingController(text: _settings.get('output.hash_output_dir', 'desktop'));
    _codecCtrl = TextEditingController(text: _settings.get('compress.codec', 'zstd'));
    _hunkSizeCtrl = TextEditingController(text: _settings.get('compress.hunk_size', '65536'));
    _unitSizeCtrl = TextEditingController(text: _settings.get('compress.unit_size', '2048'));
    _threadsCtrl = TextEditingController(text: _settings.get('compress.threads', '0'));

    final algorithms = _settings.getList('hash.algorithms', ['sha1']);
    _sha1 = algorithms.contains('sha1');
    _md5 = algorithms.contains('md5');
    _crc32 = algorithms.contains('crc32');
    _sha256 = algorithms.contains('sha256');
    _xxh3 = algorithms.contains('xxh3');

    _splitBin = _settings.getBool('extract.split_bin', false);
    _logLevel = _settings.get('app.log_level', 'info');
  }

  @override
  void dispose() {
    _extractDirCtrl.dispose();
    _compressDirCtrl.dispose();
    _hashDirCtrl.dispose();
    _codecCtrl.dispose();
    _hunkSizeCtrl.dispose();
    _unitSizeCtrl.dispose();
    _threadsCtrl.dispose();
    super.dispose();
  }

  void _save() {
    _settings.set('output.extract_output_dir', _extractDirCtrl.text);
    _settings.set('output.compress_output_dir', _compressDirCtrl.text);
    _settings.set('output.hash_output_dir', _hashDirCtrl.text);
    _settings.set('compress.codec', _codecCtrl.text);
    _settings.set('compress.hunk_size', _hunkSizeCtrl.text);
    _settings.set('compress.unit_size', _unitSizeCtrl.text);
    _settings.set('compress.threads', _threadsCtrl.text);

    final algorithms = <String>[];
    if (_sha1) algorithms.add('sha1');
    if (_md5) algorithms.add('md5');
    if (_crc32) algorithms.add('crc32');
    if (_sha256) algorithms.add('sha256');
    if (_xxh3) algorithms.add('xxh3');
    _settings.setList('hash.algorithms', algorithms);

    _settings.setBool('extract.split_bin', _splitBin);
    _settings.set('app.log_level', _logLevel);

    _settings.save();

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Settings saved'), duration: Duration(seconds: 1)),
      );
    }
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
        _buildDirField('Hash Output Directory', _hashDirCtrl, '"desktop", "logs", or absolute path'),
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
        TextField(
          controller: _codecCtrl,
          decoration: const InputDecoration(
            labelText: 'Codec',
            helperText: 'zstd, zlib, lzma, flac, cdzs, cdzl, cdlz, cdfl (comma-separated, up to 4)',
            border: OutlineInputBorder(),
          ),
        ),
        const SizedBox(height: 16),
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
        const Text('Hash Algorithms', style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        CheckboxListTile(title: const Text('SHA-1'), value: _sha1, onChanged: (v) => setState(() => _sha1 = v!)),
        CheckboxListTile(title: const Text('MD5'), value: _md5, onChanged: (v) => setState(() => _md5 = v!)),
        CheckboxListTile(title: const Text('CRC32'), value: _crc32, onChanged: (v) => setState(() => _crc32 = v!)),
        CheckboxListTile(title: const Text('SHA-256'), value: _sha256, onChanged: (v) => setState(() => _sha256 = v!)),
        CheckboxListTile(title: const Text('XXH3'), value: _xxh3, onChanged: (v) => setState(() => _xxh3 = v!)),
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
        const SizedBox(height: 16),
        ListTile(
          title: const Text('App Mode'),
          subtitle: Text(_settings.get('app.mode', 'install')),
          trailing: const Icon(Icons.info_outline),
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
            // TODO: Use file_picker to select directory
          },
        ),
      ),
    );
  }
}
