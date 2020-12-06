#!/usr/bin/env python

import rospy
from std_msgs.msg import String


def talker():
  pub = rospy.Publisher('chatter', String, queue_size=10)
  rospy.init_node('talker', anonymous=True)
  rate = rospy.Rate(10) # 10hz
  while not rospy.is_shutdown():
    hello_str = "hello world %s" % rospy.get_time()
    rospy.loginfo(hello_str)
    pub.publish(hello_str)
    rate.sleep()


# NOTE(milo): Python scripts need to be chmod'ed +x for rosrun package_name node_name to find them.
if __name__ == '__main__':
  try:
      talker()
  except rospy.ROSInterruptException:
      pass
