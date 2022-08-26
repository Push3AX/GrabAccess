/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2016  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/fdtbus.h>
#include <grub/fdt.h>
#include <grub/term.h>

static const void *dtb;
static grub_size_t root_address_cells, root_size_cells;
/* Pointer to this symbol signals invalid mapping.  */
char grub_fdtbus_invalid_mapping[1];

struct grub_fdtbus_dev *devs;
struct grub_fdtbus_driver *drivers;

int
grub_fdtbus_is_compatible (const char *compat_string,
			   const struct grub_fdtbus_dev *dev)
{
  grub_size_t compatible_size;
  const char *compatible = grub_fdt_get_prop (dtb, dev->node, "compatible",
					      &compatible_size);
  if (!compatible)
    return 0;
  const char *compatible_end = compatible + compatible_size;
  while (compatible < compatible_end)
    {
      if (grub_strcmp (compat_string, compatible) == 0)
	return 1;
      compatible += grub_strlen (compatible) + 1;
    }
  return 0;
}

static void
fdtbus_scan (struct grub_fdtbus_dev *parent)
{
  int node;
  for (node = grub_fdt_first_node (dtb, parent ? parent->node : 0); node >= 0;
       node = grub_fdt_next_node (dtb, node))
    {
      struct grub_fdtbus_dev *dev;
      struct grub_fdtbus_driver *driver;
      dev = grub_zalloc (sizeof (*dev));
      if (!dev)
	{
	  grub_print_error ();
	  return;
	}
      dev->node = node;
      dev->next = devs;
      dev->parent = parent;
      devs = dev;
      FOR_LIST_ELEMENTS(driver, drivers)
	if (!dev->driver && grub_fdtbus_is_compatible (driver->compatible, dev))
	  {
	    grub_dprintf ("fdtbus", "Attaching %s\n", driver->compatible);
	    if (driver->attach (dev) == GRUB_ERR_NONE)
	      {
		grub_dprintf ("fdtbus", "Attached %s\n", driver->compatible);
		dev->driver = driver;
		break;
	      }
	    grub_print_error ();
	  }
      fdtbus_scan (dev);
    }
}

void
grub_fdtbus_register (struct grub_fdtbus_driver *driver)
{
  struct grub_fdtbus_dev *dev;
  grub_dprintf ("fdtbus", "Registering %s\n", driver->compatible);
  grub_list_push (GRUB_AS_LIST_P (&drivers),
		  GRUB_AS_LIST (driver));
  for (dev = devs; dev; dev = dev->next)
    if (!dev->driver && grub_fdtbus_is_compatible (driver->compatible, dev))
      {
	grub_dprintf ("fdtbus", "Attaching %s (%p)\n", driver->compatible, dev);
	if (driver->attach (dev) == GRUB_ERR_NONE)
	  {
	    grub_dprintf ("fdtbus", "Attached %s\n", driver->compatible);
	    dev->driver = driver;
	  }
	grub_print_error ();
      }
}

void
grub_fdtbus_unregister (struct grub_fdtbus_driver *driver)
{
  grub_list_remove (GRUB_AS_LIST (driver));
  struct grub_fdtbus_dev *dev;
  for (dev = devs; dev; dev = dev->next)
    if (dev->driver == driver)
      {
	if (driver->detach)
	  driver->detach(dev);
	dev->driver = 0;
      }
}

void
grub_fdtbus_init (const void *dtb_in, grub_size_t size)
{
  if (!dtb_in || grub_fdt_check_header (dtb_in, size) < 0)
    grub_fatal ("invalid FDT");
  dtb = dtb_in;
  const grub_uint32_t *prop = grub_fdt_get_prop (dtb, 0, "#address-cells", 0);
  if (prop)
    root_address_cells = grub_be_to_cpu32 (*prop);
  else
    root_address_cells = 1;

  prop = grub_fdt_get_prop (dtb, 0, "#size-cells", 0);
  if (prop)
    root_size_cells = grub_be_to_cpu32 (*prop);
  else
    root_size_cells = 1;

  fdtbus_scan (0);
}

static int
get_address_cells (const struct grub_fdtbus_dev *dev)
{
  const grub_uint32_t *prop;
  if (!dev)
    return root_address_cells;
  prop = grub_fdt_get_prop (dtb, dev->node, "#address-cells", 0);
  if (prop)
    return grub_be_to_cpu32 (*prop);
  return 1;
}

static int
get_size_cells (const struct grub_fdtbus_dev *dev)
{
  const grub_uint32_t *prop;
  if (!dev)
    return root_size_cells;
  prop = grub_fdt_get_prop (dtb, dev->node, "#size-cells", 0);
  if (prop)
    return grub_be_to_cpu32 (*prop);
  return 1;
}

static grub_uint64_t
get64 (const grub_uint32_t *reg, grub_size_t cells)
{
  grub_uint64_t val = 0;
  if (cells >= 1)
    val = grub_be_to_cpu32 (reg[cells - 1]);
  if (cells >= 2)
    val |= ((grub_uint64_t) grub_be_to_cpu32 (reg[cells - 2])) << 32;
  return val;
}

static volatile void *
translate (const struct grub_fdtbus_dev *dev, const grub_uint32_t *reg)
{
  volatile void *ret;
  const grub_uint32_t *ranges;
  grub_size_t ranges_size, cells_per_mapping;
  grub_size_t parent_address_cells, child_address_cells, child_size_cells;
  grub_size_t nmappings, i;
  if (dev == 0)
    {
      grub_uint64_t val;
      val = get64 (reg, root_address_cells);
      if (sizeof (void *) == 4 && (val >> 32))
	return grub_fdtbus_invalid_mapping;
      return (void *) (grub_addr_t) val;
    }
  ranges = grub_fdt_get_prop (dtb, dev->node, "ranges", &ranges_size);
  if (!ranges)
    return grub_fdtbus_invalid_mapping;
  if (ranges_size == 0)
    return translate (dev->parent, reg);
  parent_address_cells = get_address_cells (dev->parent);
  child_address_cells = get_address_cells (dev);
  child_size_cells = get_size_cells (dev);
  cells_per_mapping = parent_address_cells + child_address_cells + child_size_cells;
  nmappings = ranges_size / 4 / cells_per_mapping;
  for (i = 0; i < nmappings; i++)
    {
      const grub_uint32_t *child_addr = &ranges[i * cells_per_mapping];
      const grub_uint32_t *parent_addr = child_addr + child_address_cells;
      grub_uint64_t child_size = get64 (parent_addr + parent_address_cells, child_size_cells);

      if (child_address_cells > 2 && grub_memcmp (reg, child_addr, (child_address_cells - 2) * 4) != 0)
	continue;
      if (get64 (reg, child_address_cells) < get64 (child_addr, child_address_cells))
	continue;

      grub_uint64_t offset = get64 (reg, child_address_cells) - get64 (child_addr, child_address_cells);
      if (offset >= child_size)
	continue;

      ret = translate (dev->parent, parent_addr);
      if (grub_fdtbus_is_mapping_valid (ret))
	ret = (volatile char *) ret + offset;
      return ret;
    }
  return grub_fdtbus_invalid_mapping;
}

volatile void *
grub_fdtbus_map_reg (const struct grub_fdtbus_dev *dev, int regno, grub_size_t *size)
{
  grub_size_t address_cells, size_cells;
  address_cells = get_address_cells (dev->parent);
  size_cells = get_size_cells (dev->parent);
  const grub_uint32_t *reg = grub_fdt_get_prop (dtb, dev->node, "reg", 0);
  if (size && size_cells)
    *size = reg[(address_cells + size_cells) * regno + address_cells];
  if (size && !size_cells)
    *size = 0;
  return translate (dev->parent, reg + (address_cells + size_cells) * regno);
}

const char *
grub_fdtbus_get_name (const struct grub_fdtbus_dev *dev)
{
  return grub_fdt_get_nodename (dtb, dev->node);
}

const void *
grub_fdtbus_get_prop (const struct grub_fdtbus_dev *dev,
		      const char *name,
		      grub_uint32_t *len)
{
  return grub_fdt_get_prop (dtb, dev->node, name, len);
}

const void *
grub_fdtbus_get_fdt (void)
{
  return dtb;
}
