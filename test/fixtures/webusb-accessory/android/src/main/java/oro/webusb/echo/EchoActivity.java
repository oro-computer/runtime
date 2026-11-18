package oro.webusb.echo;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class EchoActivity extends Activity {
  private TextView status;
  private UsbManager manager;
  private InputStream input;
  private OutputStream output;
  private final BroadcastReceiver detached = new BroadcastReceiver() {
    @Override public void onReceive(Context context, Intent intent) {
      closeAccessory();
      show("USB disconnected");
    }
  };

  @Override public void onCreate(Bundle state) {
    super.onCreate(state);
    status = new TextView(this);
    status.setPadding(48, 120, 48, 48);
    status.setTextSize(20);
    setContentView(status);
    manager = getSystemService(UsbManager.class);
    registerReceiver(detached, new IntentFilter(UsbManager.ACTION_USB_ACCESSORY_DETACHED),
        Context.RECEIVER_NOT_EXPORTED);
  }

  @Override public void onResume() {
    super.onResume();
    if (input != null) return;
    UsbAccessory[] devices = manager.getAccessoryList();
    if (devices != null) {
      for (UsbAccessory device : devices) {
        if (!"Oro Runtime".equals(device.getManufacturer()) ||
            !"WebUSB Bulk Echo".equals(device.getModel()) || !"1".equals(device.getVersion())) continue;
        if (!manager.hasPermission(device)) {
          show("Choose Oro USB Echo in the USB accessory prompt.");
          return;
        }
        try {
          input = manager.openAccessoryInputStream(device);
          output = manager.openAccessoryOutputStream(device);
          InputStream reader = input;
          OutputStream writer = output;
          String serial = device.getSerial();
          if (serial == null || !serial.matches("[0-9a-f]{16}")) {
            closeAccessory();
            show("Invalid echo session identifier");
            return;
          }
          show("Ready for bulk echo");
          byte[] prefix = serial.getBytes(StandardCharsets.US_ASCII);
          new Thread(() -> echo(reader, writer, prefix), "usb-bulk-echo").start();
        } catch (RuntimeException e) {
          closeAccessory();
          show("Open failed: " + e);
        }
        return;
      }
    }
    show("Waiting for the Linux host to start accessory mode.");
  }

  private void echo(InputStream reader, OutputStream writer, byte[] prefix) {
    byte[] buffer = new byte[16384];
    String result = "USB stream ended";
    try {
      int count;
      while ((count = reader.read(buffer)) > 0) {
        if (count < prefix.length || !Arrays.equals(buffer, 0, prefix.length, prefix, 0, prefix.length)) {
          continue;
        }
        writer.write(buffer, 0, count);
        writer.flush();
        String message = "Echoed " + count + " bytes";
        runOnUiThread(() -> { if (input == reader) show(message); });
      }
    } catch (IOException | RuntimeException e) {
      result = "USB stream closed: " + e;
    }
    String message = result;
    runOnUiThread(() -> {
      if (input == reader) {
        closeAccessory();
        show(message);
      }
    });
  }

  private void show(String message) {
    status.setText(message);
    Log.i("OroUsbEcho", message);
  }

  private static void closeStream(Closeable stream) {
    if (stream == null) return;
    try {
      stream.close();
    } catch (IOException e) {
      Log.w("OroUsbEcho", "Closing accessory stream", e);
    }
  }

  private void closeAccessory() {
    closeStream(input);
    closeStream(output);
    input = null;
    output = null;
  }

  @Override public void onDestroy() {
    unregisterReceiver(detached);
    closeAccessory();
    super.onDestroy();
  }
}
