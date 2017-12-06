import socket
import sys
import requests
import json
import decimal
import re

"""
Message format:

For stocks: stock_<TICKR>
For weather: weather_<ZIP>

"""

finance_url = "https://236936f3d28e51f8bb1ef2726ec25d87:8e19b31fdfc8d6c4b0cf3d62c985177e@api.intrinio.com/data_point?identifier={}&item=last_price"
weather_url = "http://api.openweathermap.org/data/2.5/weather?zip={},us&APPID=d5015b2ecb699fae01c227c8edcdf15c"
# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Connect the socket to the port where the server is listening
server_address = ('10.148.8.34', 333) #Enter IP here
print >>sys.stderr, 'connecting to %s port %s' % server_address
sock.connect(server_address)
print >>sys.stderr, 'Connected!'

stock_price_test = ['169.700','169.600','169.800','169.700']
y =0

try:
	sock.sendall('Connected\r\n')
	while True:
		data = sock.recv(16) #receive and return a message <16 bytes
		print >>sys.stderr, 'received "%s"' % data
		data = data.strip()
		if 'stock' in data:
			stock_symbol = data[(data.find('_')+1):]
			stock_url = finance_url.format(stock_symbol)
			try:
				myResponse = requests.get(stock_url) #HTTP get request
				stock_price = str(json.loads(myResponse.content)['value'])
				try:
					x = float(stock_price)
					#sock.sendall(stock_price_test[y]) #for testing
					#print stock_price_test[y]
					#y = y+1
					#if y==4: y=0
					while(len(stock_price)<7):
						stock_price = stock_price + '0'
					sock.sendall(stock_price)
				except:
					sock.sendall('Errorrr') #needs to be 7 char
					print "Error"
				

					
			except:
				sock.sendall('Error')
				print "Error"
			
		elif 'weather' in data:
			zip_code = data[(data.find('_')+1):]
			zip_url = weather_url.format(zip_code)
			myResponse = requests.get(zip_url)
			temperature = json.loads(myResponse.content)['main']['temp']
			sock.sendall(str(temperature))
			print temperature

finally:
	print >>sys.stderr, 'Closing socket'
	sock.close()
