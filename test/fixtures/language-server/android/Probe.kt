package fixture

import android.content.Context
import android.content.Intent
import androidx.core.content.ContextCompat
import oro.runtime.app.NativeExtension
import oro.runtime.app.DeferredReadExecutor

class Probe(
  val intent: Intent,
  val extension: NativeExtension,
  val executor: DeferredReadExecutor,
  val sibling: Sibling
) {
  fun siblingValue(): String = sibling.value
  fun color(context: Context): Int = ContextCompat.getColor(context, android.R.color.black)
}
