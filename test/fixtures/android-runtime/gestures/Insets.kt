package androidx.core.view
import android.view.View
class Insets(val left: Int=0,val top: Int=0,val right: Int=0,val bottom: Int=80)
class WindowInsetsCompat {
 object Type { fun mandatorySystemGestures() = 1 }
 fun getInsets(type: Int)=Insets()
}
object ViewCompat { fun getRootWindowInsets(view: View) = WindowInsetsCompat() }
