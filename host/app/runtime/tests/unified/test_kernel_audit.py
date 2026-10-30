"""Small hand-declared HWH fixtures; actual kernel/DT compilation remains separate."""
import unittest
import hashlib,json
from scripts.unified.audit_kernel import parse_design_hwh

XML=b'''<SYSTEM VIVADOVERSION="2020.1"><MODULES>
<MODULE INSTANCE="ps" MODTYPE="zynq_ultra_ps_e"><PARAMETERS>
<PARAMETER NAME="PSU__UART0__PERIPHERAL__ENABLE" VALUE="1"/>
<PARAMETER NAME="PSU__ENET3__PERIPHERAL__ENABLE" VALUE="1"/>
</PARAMETERS></MODULE></MODULES><MEMORYMAP>
<MEMRANGE INSTANCE="HPM" BASEVALUE="0xA0000000" HIGHVALUE="0xA000FFFF"/>
<MEMRANGE INSTANCE="cdma" BASEVALUE="0xB0010000" HIGHVALUE="0xB001FFFF"/>
<MEMRANGE INSTANCE="status_gpio" BASEVALUE="0xB0020000" HIGHVALUE="0xB002FFFF"/>
<MEMRANGE INSTANCE="ddr" BASEVALUE="0x500000000" HIGHVALUE="0x5FFFFFFFF"/>
</MEMORYMAP></SYSTEM>'''

class KernelHardwareAudit(unittest.TestCase):
 def test_real_address_domains_are_explicit_and_bounded(self):
  row=parse_design_hwh(XML)
  self.assertEqual(row.get('registers'),{'gpu':[0xa0000000,65536],'cdma':[0xb0010000,65536],'ready_gpio':[0xb0020000,65536]})
  self.assertEqual(row.get('pl_pool'),[0x500000000,0x80000000])
  self.assertIsNone(row.get('gpu_raw_cycles'))
 def test_old_or_new_tool_version_is_rejected(self):
  with self.assertRaises(ValueError):parse_design_hwh(XML.replace(b'2020.1',b'2020.2'))
 def test_wrong_cdma_map_and_short_ddr_are_rejected(self):
  for old,new in [(b'0xB0010000',b'0xA2000000'),(b'0x5FFFFFFFF',b'0x53FFFFFFF')]:
   with self.subTest(new=new),self.assertRaises(ValueError):parse_design_hwh(XML.replace(old,new))
 def test_ps_parameters_have_order_independent_exact_identity(self):
  a=b'<PARAMETER NAME="PSU__UART0__PERIPHERAL__ENABLE" VALUE="1"/>'
  b=b'<PARAMETER NAME="PSU__ENET3__PERIPHERAL__ENABLE" VALUE="1"/>'
  first=parse_design_hwh(XML);second=parse_design_hwh(XML.replace(a+b'\n'+b,b+b'\n'+a))
  self.assertEqual(first.get('ps_parameter_count'),2)
  self.assertEqual(first.get('ps_parameters_sha256'),second.get('ps_parameters_sha256'))
  self.assertNotEqual(first.get('ps_parameters_sha256'),parse_design_hwh(XML.replace(b'VALUE="1"',b'VALUE="0"',1)).get('ps_parameters_sha256'))
 def test_ambiguous_duplicate_ps_parameters_are_rejected(self):
  with self.assertRaises(ValueError):parse_design_hwh(XML.replace(b'</PARAMETERS>',b'<PARAMETER NAME="PSU__UART0__PERIPHERAL__ENABLE" VALUE="0"/></PARAMETERS>'))
 def test_conflicting_duplicate_address_is_rejected(self):
  extra=b'<MEMRANGE INSTANCE="cdma" BASEVALUE="0xB0030000" HIGHVALUE="0xB003FFFF"/>'
  with self.assertRaises(ValueError):parse_design_hwh(XML.replace(b'</MEMORYMAP>',extra+b'</MEMORYMAP>'))
 def test_unset_optional_PS_value_is_preserved_without_guessing_default(self):
  raw=XML.replace(b'NAME="PSU__UART0__PERIPHERAL__ENABLE" VALUE="1"',b'NAME="PSU__UART0__PERIPHERAL__ENABLE"')
  row=parse_design_hwh(raw)
  expected={'PSU__UART0__PERIPHERAL__ENABLE':None,'PSU__ENET3__PERIPHERAL__ENABLE':'1'}
  digest=hashlib.sha256(json.dumps(expected,sort_keys=True,separators=(',',':')).encode()).hexdigest()
  self.assertEqual(row['ps_parameters_sha256'],digest)
  self.assertNotEqual(digest,parse_design_hwh(raw.replace(b'NAME="PSU__UART0__PERIPHERAL__ENABLE"',b'NAME="PSU__UART0__PERIPHERAL__ENABLE" VALUE=""'))['ps_parameters_sha256'])

if __name__=='__main__':unittest.main()
