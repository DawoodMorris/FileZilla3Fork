#! /usr/bin/python

import MySQLdb
import os
import sys
import time
import rrdtool

import database
db = MySQLdb.connect(db=database.database, user=database.user, passwd=database.password)

colors = [ '#009933', '#003399', '#993300', '#bbbb00' ]

def create_graph(file, title, label_y, mode, data):
 
  query = 'SELECT revisions.date'
  for line in data:
    query += ', metrics.'+ line[1]
  query += ' FROM metrics, revisions WHERE metrics.revision = revisions.revision'
  
  cursor = db.cursor()
  cursor.execute(query)
  
  initialised = False
  prev=0
  starttime=0
  while True:
    row = cursor.fetchone()
    if row == None:
      break;
    
    
  
    timestamp = int(time.mktime(time.strptime(str(row[0]), '%Y-%m-%d %H:%M:%S')))
  
    if not initialised:
      args = [ '/home/metrics/tmp/tmp.rrd', 
               '--start', str(timestamp - 1)
             ]

      for i, line in enumerate(data):
        args.append('DS:source' + str(i) + ':GAUGE:9999999:U:U')

      args.append('RRA:AVERAGE:0.1:22:2200200')
      rrdtool.create(*args)
      initialised = True
      starttime=timestamp
  
    if prev != timestamp:

      s = str(timestamp)
      for i in range(1, len(data) + 1):
        if row[i] == None:
	  s += ':U'
	else:
          s += ':%d' % row[i]
      rrdtool.update('../tmp/tmp.rrd', s)
      prev = timestamp
  
  cursor.close()

  args = [file,
          '--imgformat', 'PNG',
          '--width', '520',
          '--height', '250',
          '--start', "%d" % starttime,
#         '--end', "-1",
          '--vertical-label', label_y,
          '--title', title,
          '--lower-limit', '0']

  for i, line in enumerate(data):

    flags = line[2:]
    args.append('DEF:source' + str(i) + '=../tmp/tmp.rrd:source' + str(i) + ':AVERAGE')

    if 'area' in flags:
      graph = 'AREA:'
    else:
      graph = 'LINE:'
    graph += 'source' + str(i) + colors[i] +':' + line[0]

    if 'stack' in flags:
      graph += ':STACK'
    args.append(graph)

  rrdtool.graph(*args)

data = [ ('Source lines', 'loc_source'), ('Header lines', 'loc_header'), ('Other text files', 'loc_other') ]
create_graph('/var/www/code/test.png', 'Lines of code', 'LOC', 'normal', data)

data = [ ('Source files', 'count_source'), ('Header files', 'count_header'), ('Other text files', 'count_other'), ('Binary files', 'count_binary') ]
create_graph('/var/www/code/test2.png', 'Number of files', '# Files', 'normal', data)

data = [ ('Source files', 'size_source'), ('Header files', 'size_header'), ('Other text files', 'size_other'), ('Binary files', 'size_binary') ]
create_graph('/var/www/code/test3.png', 'Total filesize', 'Size (bytes)', 'normal', data)

data = [ ('Source files', 'size_source', 'area'), ('Header files', 'size_header', 'stack', 'area'), ('Other text files', 'size_other', 'stack', 'area'), ('Binary files', 'size_binary', 'stack', 'area') ]
create_graph('/var/www/code/test4.png', 'Total filesize (additive)', 'Size (bytes)', 'normal', data)

data = [ ('Source files', 'size_source / count_source'), ('Header files', 'size_header / count_header'), ('Other text files', 'size_other / count_other'), ('Binary files', 'size_binary / count_binary') ]
create_graph('/var/www/code/test5.png', 'Average filesize', 'Size (bytes)', 'normal', data)

data = [ ('Source files', 'loc_source / count_source'), ('Header files', 'loc_header / count_header'), ('Other text files', 'loc_other / count_other') ]
create_graph('/var/www/code/test6.png', 'Average lines of code per file', 'LOC', 'normal', data)

db.close()

