                    ****************************************
                                     README

                    ****************************************
	
                         Chelsio Terminator Firmware
		           

                             Version : 1.27.5.0
                             Date    : 05/16/2025



Overview
================================================================================

Chelsio Terminator firmware package contains firmware binary files used to 
update the firmware on Unified Wire adapters. Although the adapters are shipped 
with the correct firmware and the driver will update the firmware automatically;
if required, you can manually update firmware by using this package.



================================================================================
  CONTENTS
================================================================================

- 1. Supported Cards
- 2. How to Use
- 3. Support Documentation
- 4. Customer Support



1. Supported Cards
================================================================================

- Chelsio adapter



2. How to Use
================================================================================

a) Download the firmware package.

b) Extract the package.
    
   [root@host~]# unzip Chelsio-t6fw-x.x.x.x.zip

c) Load the network driver.

   [root@host~]# modprobe cxgb4

d) Flash the firmware using cudbg_app

   [root@host~]# cd Chelsio-t6fw-x.x.x.x
   [root@host~]# ./cudbg_app --loadfw <ethX> t6fw-1.27.5.0.bin --force

e) Reload the drivers.

   [root@host~]# rmmod iw_cxgb4
   [root@host~]# rmmod cxgb4
   [root@host~]# modprobe cxgb4

f) Verify the firmware version:
 
  [root@host~]# ethtool -i <ethX>


    
3. Support Documentation
================================================================================

The documentation for this release can be found in the Chelsio-t6fw-x.x.x.x 
directory. It contains: 

- README
- Release Notes



4. Customer Support
================================================================================

Please contact Chelsio support at support@chelsio.com for any issues regarding 
the product.








********************************************************************************
Copyright (C) 2025 Chelsio Communications. All Rights Reserved.

The information in this document is furnished for informational use only, is
subject to change without notice, and should not be construed as a commitment by
Chelsio Communications. Chelsio Communications assumes no responsibility or
liability for any errors or inaccuracies that may appear in this document or any
software that may be provided in association with this document. Except as
permitted by such license, no part of this document may be reproduced, stored in
a retrieval system,or transmitted in any form or by any means without the
express written consent of Chelsio Communications.
