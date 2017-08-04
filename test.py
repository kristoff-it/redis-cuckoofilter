

def haszero(v):
	return (((v) - 0x0101010101010101) & ~(v) & 0x8080808080808080)

def hasvalue(x,n):
	return (haszero((x) ^ ((0xffffffffffffffff)//255 * (n))))

def haszero1(v):
	return (((v) - 0x0100010001000100) & ~(v) & 0x8000800080008000)
def hasvalue1(x,n):
	return (haszero1((x) ^ ((0xffffffffffffffff)//65535 * (n))))
