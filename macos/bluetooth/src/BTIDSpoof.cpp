#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSNumber.h>

class BTIDSpoof : public IOService {
    OSDeclareDefaultStructors(BTIDSpoof)
public:
    bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(BTIDSpoof, IOService)

bool BTIDSpoof::start(IOService *provider) {
    if (!provider || !IOService::start(provider)) return false;
    OSNumber *vendor = OSDynamicCast(OSNumber, provider->getProperty("idVendor"));
    OSNumber *product = OSDynamicCast(OSNumber, provider->getProperty("idProduct"));
    if (!vendor || !product || vendor->unsigned32BitValue() != 0x13d3 ||
        product->unsigned32BitValue() != 0x3571) {
        IOLog("BTIDSpoof: unexpected provider IDs; leaving unchanged\n");
        IOService::stop(provider);
        return false;
    }
    const unsigned oldProduct = product->unsigned32BitValue();
    const bool changed = provider->setProperty("idProduct", (unsigned long long)0x3568, 32);
    OSNumber *after = OSDynamicCast(OSNumber, provider->getProperty("idProduct"));
    const unsigned newProduct = after ? after->unsigned32BitValue() : 0;
    IOLog("BTIDSpoof: vendor=0x13d3 idProduct 0x%04x -> 0x%04x setProperty=%s\n",
          oldProduct, newProduct, changed ? "true" : "false");
    if (!changed || newProduct != 0x3568) {
        IOService::stop(provider);
        return false;
    }
    return true;
}
