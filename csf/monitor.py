#!/usr/bin/python
# Python client of Monitor 3.0
# Version: 3.0
# Date: 2009-12-13
# Last update: 2009-12-21
# Avin, Zhangshuo <zhangshuo_a@aspire-info.com>

import os
import sys
import json
import time
import signal
import locale
import string
import httplib, urllib
from pprint import *
from optparse import OptionParser

#connection settings
cs = {
	'host': '127.0.0.1',
	'port': 33333,
	'timeout': 5,
}

conf = {
	'color': True,
}


'''
http transportation
connect, close and request by URL
'''
class http_transfer :
	def connect(self, host, port, timeout) :
		try :
			self.host = host
			self.port = port
			self.timeout = timeout
			self.conn = httplib.HTTPConnection(host, port, timeout)
		except Exception,e :
			print "can not connect to server: ", e
			raise e

	def get_by_url(self, url) :
		for x in range(3) :
			try :
				self.conn.request("GET",  url)
				r = self.conn.getresponse()
				data = r.read()
				return data
			except Exception,e :
				print "Error. Trying to reconnect..."  
				self.conn = httplib.HTTPConnection(self.host, self.port, self.timeout)
				continue
		else :
			print "Can not get data: ", e
			raise e
			
	def close(self) :
		try :
			self.conn.close()
		except Exception,e :
			print "error when close: ", e
			raise e

'''
sub class the Option Parser
override the error and exit method to suit my needs.
'''
class optparser(OptionParser) :
	def error(self, msg) :
		raise Exception()
	
	def exit(self) :
		return None

'''
main class of the monitor.
create HTTP connections, parse user command, format the data and output.
'''
class mnt_main :
	def __init__(self) :
		try :
			#parse command line		
			usage = "usage: %prog [host] [options]"
			parser = OptionParser(usage = usage)
			parser.add_option("-b", action="store_true", dest="nocolor", help="disable color support.")
			parser.add_option("-p", dest="port", help="specify the port of server.")
			(options, args) = parser.parse_args(sys.argv[1:])
			
			if options.nocolor :
				conf['color'] = False
			if options.port :
				cs['port'] = options.port
			if len(args) > 0 and args[0] :
				cs['host'] = args[0]
			
			#set the signal handler
			signal.signal(signal.SIGINT, self.signal_handle)
			self.stat_trigger = False
			
			#init http transfer
			self.http = http_transfer()
			self.http.connect(cs['host'], cs['port'], cs['timeout'])
			print "Testing remote http server..."
			self.cmd_info()
			print "Test OK. Monitor ready.\n"
			self.help_msg()
		
			while True :
				command = raw_input(">>> ")
				r = self.cmd_process(command)
				print r
			
		except Exception,e :
			print "\nMonitor report: ", e
			#raw_input()
			import traceback
			print "-" * 80
			traceback.print_exc()
			sys.exit()

	def signal_handle(self, signo, o) :
		if signo == 2 :
			self.stat_trigger = False
	
	def cmd_process(self, cmd) :
		global stat_interval
		global stat_groups
	
		if cmd == "gl" :
			print self.cmd_grouplist()
			return ""
		elif cmd == "il" :
			print self.cmd_itemlist()
			return ""
		elif cmd == "info" :
			print self.cmd_info()
			return ""
		elif cmd == "help" :
			print self.cmd_help()
			return ""
		elif cmd == "quit" :
			self.cmd_quit()
			return ""
		elif len(cmd) >=4 and cmd[:4] == "stat" :
			try :
				stat_groups = []
				exc_groups = []
				act_interval = 1
				
				parser = optparser()
				parser.add_option("-e", dest = "ex_set")
				parser.add_option("-t", dest = "interval")
				(options, args) = parser.parse_args(cmd.split(" ")[1:])
				
				if len(args) == 1 and args[0] == "*" :
					stat_groups = self.cmd_grouplist().split(" ")
				else :
					stat_groups = args[0].split(",") 
				#print "stat_groups:", stat_groups
				
				if options.ex_set :
					exc_groups = options.ex_set.split(",")
				#print "exc_groups:", exc_groups
					
				final_groups = list(set(stat_groups) - set(exc_groups))
				final_groups.sort()
				#print "final_groups:", final_groups
				
				if options.interval :
					act_interval = int(options.interval)
				#print "internal:", act_interval 
				
				data_dict = dict()
				self.stat_trigger = True
				try :
					while True :
						if not self.stat_trigger :
							break
						print self.cmd_stat(final_groups, data_dict)
						time.sleep(act_interval)
				except KeyboardInterrupt :
					print "STAT: User Interrupt."
				except TypeError :
					print "STAT: No such data."
				except Exception,e :
					raise
						
				return ""

			except Exception,e :
				print "STAT: syntax error."
				#import traceback
				#print "-" * 80
				#traceback.print_exc()
				#sys.exit()
	
		else :
			print "Command not found."
			return ""
			
	def cmd_stat(self, groups, data_dict) :
		url = "/command?stat|" + "|".join(groups)

		data = self.json_parse(self.http.get_by_url(url))
		groups_array = []
		groups_dict = data_dict
		
		#############################
		#assemble data
		#############################
		for g_key in data[0] :	
			g_val = data[0][g_key]
			g_dict = dict()
			g_array = []
			for i in g_val :
				row_value = [ None for x in range(7) ]
				row_h_value = []
				if groups_dict and g_key in groups_dict and i in groups_dict[g_key] :
					row_h_value = groups_dict[g_key][i][6]
					#print "----row_h_value got!!!!"
					#print "row_h_value:", row_h_value
				
				row_g_name = g_key				#group name
				row_i_name = i					#item name		
				if g_val[i].isdigit() :		#item value
					row_i_value = int(g_val[i])		
				else :
					row_i_value = g_val[i]
					
				#pprint(g_val[i])
					
				if len(row_h_value) > 0 :
					row_l_value = row_h_value[-1]		#last item value
				else :
					row_l_value = 0
				
				row_h_value.append(row_i_value)			#last 10 histroy value
				if len(row_h_value) > 10 :
					row_h_value = row_h_value[1:]

				#rate of increment. ONLY efficient when type is positive integer
				#the average of last 10 values
				if type(row_i_value) == type(0) :
					if row_l_value != 0 :
						row_iir_value = str("%0.2f" % ((float((row_i_value - row_l_value)) / row_l_value) * 100)) + "%"
					else :
						row_iir_value = "-"
					if len(row_h_value) != 0 :
						row_avg_value = "%0.2f" % (float(sum(row_h_value)) / len(row_h_value))
					else :
						row_avg_value = "-"
				else :
					row_iir_value = "-"
					row_avg_value = "-"
					
				row_value[0] = row_g_name		#group name
				row_value[1] = row_i_name		#item name
				row_value[2] = row_i_value		#item value
				row_value[3] = row_l_value		#last item value
				row_value[4] = row_iir_value	#rate of increment
				row_value[5] = row_avg_value	#average value
				row_value[6] = row_h_value		#histroy value
				
				g_dict[i] = row_value
				g_array.append(row_value)
			#end for
			groups_dict[g_key] = g_dict
			groups_array.append(g_array)
			
		#end for
		
		#############################
		#format and output
		#############################
		col_title = ["Group", "Item", "Value", "Last", "Inc.(%)", "Avg.(L10)"]
		col_title[0] = "%14s" % (col_title[0])
		col_title[1] = "%14s" % (col_title[1])
		col_title[2] = "%14s" % (col_title[2])
		col_title[3] = "%14s" % (col_title[3])
		col_title[4] = "%10s" % (col_title[4])
		col_title[5] = "%14s" % (col_title[5])
		#col_title = [ "%12s" % (x) for x in col_title ]
		col_title = "".join(col_title)
		
		formatted = col_title + '\n'
		formatted += ('=' * 80) + '\n'
		
		groups_array.sort()
		for x in groups_array :
			x.sort()

		for g in groups_array :
			row_formatted = []
			is_gname_output = False
			for i in g :
				row_formatted = i[:-1]		#remove histroy value
				#row_formatted = [ "%12s" % (str(x)[:11]) for x in row_formatted[:-1] ]
				if is_gname_output :
					row_formatted[0] = "%14s" % ("")
				else :
					row_formatted[0] = "%14s" % (str(row_formatted[0])[:13])
					is_gname_output = True
				row_formatted[1] = "%14s" % (str(row_formatted[1])[:13])
				
				row_formatted[2] = str(row_formatted[2])
				is_value_changed = False
				if row_formatted[2] != row_formatted[3] :
					is_value_changed = True

				if row_formatted[2].isdigit() :
					row_formatted[2] = self.add_digit_group_symbol(row_formatted[2])
						
				if is_value_changed :
					row_formatted[2] = "%14s" % (str(row_formatted[2]))
					#color support
					if conf['color'] :
						row_formatted[2] = colored(str(row_formatted[2]), 'red', attrs=['bold'])
				else :
					row_formatted[2] = "%14s" % (str(row_formatted[2])[:13])
	
				row_formatted[3] = str(row_formatted[3])
				if row_formatted[3].isdigit() :
					row_formatted[3] = self.add_digit_group_symbol(row_formatted[3])
				row_formatted[3] = "%14s" % (row_formatted[3][:13])
				row_formatted[4] = "%10s" % (str(row_formatted[4])[:9])
				row_formatted[5] = "%14s" % (str(row_formatted[5])[:13])
				
				row_formatted = "".join(row_formatted)
				formatted += (row_formatted + '\n')
			formatted += ('-' * 80) + '\n'

		#print col_title
		#pprint(groups_dict)

		return formatted
		
	def cmd_grouplist(self) :
		url = "/command?grouplist"
		json_data = self.json_parse(self.http.get_by_url(url))
		json_data = " ".join(json_data)
		return json_data
		
	def cmd_itemlist(self) :
		url = "/command?itemlist"
		data = self.json_parse(self.http.get_by_url(url))
		g_name = [ ([y for y in x][0]) for x in data ]
		g_item = [ x[[ y for y in x ][0]] for x in data ]
		str = "\n".join([ g_name[i]+": "+" ".join(g_item[i]) for i in range(len(g_name))])
		return str
		
	def cmd_help(self) :
		#url = "/command?help"
		#json_data = self.json_parse(self.http.get_by_url(url))
		#return json_data[0]
		self.help_msg()
		return ""
		
	def cmd_info(self) :
		url = "/command?info"
		json_data = self.json_parse(self.http.get_by_url(url))
		return json_data[0]
		
	def cmd_quit(self) :
		print "Quit...", "Closing http connection..."
		self.http.close()
		print "Ready to quit."
		sys.exit()
		return ""
	
	def json_parse(self, json_str) :
		try :
			return json.loads(json_str)
		except Exception,e :
			print "can not parse json string: ", e, "\n"
			
	def add_digit_group_symbol(self, str) :
		# digital group symbol
		if (str.isdigit()) :
			digital_str_array = []
			digital_str = range(len(str), -1, -3)
			digital_str_split = zip(digital_str, digital_str[1: ] + [0, ])
			digital_str_split.reverse()	
			for x,y in digital_str_split :
				temp = str[y : x]
				if temp != "" :
					digital_str_array.append(temp)
				r = ','.join(digital_str_array)	
		return r
			
	def help_msg(self) :
		print "Python client 1.0 for libMonitor 3.0"
		print "\n---------------------- Inner Command ----------------------\n"
		print "gl: list the groups." 
		print "il: list the items in every group." 
		print "info: show the server side information."
		print "help: show the client side help message."
		print "quit: quit this programme."
		print "stat <names> [-e gname] [-a interval]: show the value of specific group(s)."
		print "\n[options]"
		print "\tnames: groups with name supplied. use ',' to seperate mutiple groups."
		print "\t       charactor '*' means match as much groups as possible."
		print "\t-e gname: familar with the description above, but exclude."
		print "\t-t interval: attach to trigger which will be trigged every 'interval' second(s)."
		print "\teg: stat * -e group1 -t 1\n"
		

# Copyright (C) 2008 Konstantin Lepa <konstantin.lepa@gmail.com>.
#
# This file is part of termcolor.
#
# termcolor is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3, or (at your option) any later
# version.
#
# termcolor is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with termcolor.  If not, see <http://www.gnu.org/licenses/>.

__ALL__ = [ 'colored' ]
attributes = dict(zip(['bold','dark','','underline','blink','','reverse','concealed'],range(1, 9)))
del attributes['']
highlights = dict(zip(['on_grey','on_red','on_green','on_yellow','on_blue','on_magenta','on_cyan','on_white'],range(40, 48)))
colors = dict(zip(['grey','red','green','yellow','blue','magenta','cyan','white',],range(30, 38)))

def colored(text, color=None, on_color=None, attrs=None):
	if os.getenv('ANSI_COLORS_DISABLED') is None:
		fmt_str = '\033[1;%dm%s'
		if color is not None:
			text = fmt_str % (colors[color], text)

		if on_color is not None:
			text = fmt_str % (highlights[on_color], text)

		if attrs is not None:
			for attr in attrs:
				text = fmt_str % (attributes[attr], text)

		reset = '\033[1;m'
		text += reset

	return text


###########################################################

if __name__ == "__main__" :
	locale.setlocale(locale.LC_ALL, "")
	mnt = mnt_main()

