#
# Regular cron jobs for the mediasynclite package
#
0 4	* * *	root	[ -x /usr/bin/mediasynclite_maintenance ] && /usr/bin/mediasynclite_maintenance
