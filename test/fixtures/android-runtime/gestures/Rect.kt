package android.graphics
class Rect(var left: Int, var top: Int, var right: Int, var bottom: Int) {
 fun intersect(other: Rect): Boolean {
  if(left >= other.right || other.left >= right || top >= other.bottom || other.top >= bottom) return false
  left=maxOf(left,other.left);top=maxOf(top,other.top);right=minOf(right,other.right);bottom=minOf(bottom,other.bottom);return true
 }
}
