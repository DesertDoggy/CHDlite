import 'package:flutter_test/flutter_test.dart';
import 'package:chdlite/main.dart';

void main() {
  testWidgets('App launches', (WidgetTester tester) async {
    await tester.pumpWidget(const CHDliteApp());
    expect(find.text('CHDlite'), findsOneWidget);
  });
}
