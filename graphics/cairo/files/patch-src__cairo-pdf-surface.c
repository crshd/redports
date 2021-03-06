--- src/cairo-pdf-surface.c.orig	2012-10-25 14:50:57.603523484 +0800
+++ src/cairo-pdf-surface.c	2012-10-25 14:51:11.513553899 +0800
@@ -1135,6 +1135,7 @@
 _cairo_pdf_surface_add_source_surface (cairo_pdf_surface_t	*surface,
 				       cairo_surface_t		*source,
 				       cairo_filter_t		 filter,
+				       cairo_bool_t              mask,
 				       cairo_pdf_resource_t	*surface_res,
 				       int                      *width,
 				       int                      *height)
@@ -1177,6 +1178,7 @@
 
     surface_entry->id = surface_key.id;
     surface_entry->interpolate = interpolate;
+    surface_entry->mask = mask;
     _cairo_pdf_source_surface_init_key (surface_entry);
 
     src_surface.hash_entry = surface_entry;
@@ -1783,6 +1785,46 @@
     return TRUE;
 }
 
+static cairo_status_t
+_cairo_pdf_surface_emit_imagemask (cairo_pdf_surface_t	 *surface,
+				   cairo_image_surface_t *image,
+				   cairo_pdf_resource_t	 *image_res)
+{
+    cairo_status_t status;
+    uint8_t *byte, output_byte;
+    int row, col, num_cols;
+
+
+    /* This is the only image format supported for stencil masking */
+    assert (image->format == CAIRO_FORMAT_A1);
+
+    status = _cairo_pdf_surface_open_stream (surface,
+					     image_res,
+					     TRUE,
+					     "   /Type /XObject\n"
+					     "   /Subtype /Image\n"
+					     "   /ImageMask true\n"
+					     "   /Width %d\n"
+					     "   /Height %d\n"
+					     "   /BitsPerComponent 1\n",
+					     image->width, image->height);
+    if (unlikely (status))
+	return status;
+
+    num_cols = (image->width + 7) / 8;
+    for (row = 0; row < image->height; row++) {
+	byte = image->data + row * image->stride;
+	for (col = 0; col < num_cols; col++) {
+	    output_byte = CAIRO_BITSWAP8_IF_LITTLE_ENDIAN (*byte);
+	    output_byte = ~output_byte;
+	    _cairo_output_stream_write (surface->output, &output_byte, 1);
+	    byte++;
+	}
+    }
+
+    return _cairo_pdf_surface_close_stream (surface);
+}
+
 /* Emit alpha channel from the image into the given data, providing
  * an id that can be used to reference the resulting SMask object.
  *
@@ -1891,7 +1933,8 @@
 _cairo_pdf_surface_emit_image (cairo_pdf_surface_t     *surface,
                                cairo_image_surface_t   *image,
                                cairo_pdf_resource_t    *image_res,
-			       cairo_filter_t           filter)
+			       cairo_filter_t           filter,
+			       cairo_bool_t             mask)
 {
     cairo_status_t status = CAIRO_STATUS_SUCCESS;
     char *rgb;
@@ -1912,6 +1955,9 @@
 	    image->format == CAIRO_FORMAT_A8 ||
 	    image->format == CAIRO_FORMAT_A1);
 
+    if (mask)
+	return _cairo_pdf_surface_emit_imagemask (surface, image, image_res);
+
     rgb_size = image->height * image->width * 3;
     rgb = _cairo_malloc_abc (image->width, image->height, 3);
     if (unlikely (rgb == NULL)) {
@@ -2108,7 +2154,8 @@
 _cairo_pdf_surface_emit_image_surface (cairo_pdf_surface_t     *surface,
 				       cairo_surface_t         *source,
 				       cairo_pdf_resource_t     resource,
-				       cairo_bool_t 		interpolate)
+				       cairo_bool_t 		interpolate,
+				       cairo_bool_t             mask)
 {
     cairo_image_surface_t *image;
     void *image_extra;
@@ -2127,7 +2174,7 @@
 	return status;
 
     status = _cairo_pdf_surface_emit_image (surface, image,
-					    &resource, interpolate);
+					    &resource, interpolate, mask);
     if (unlikely (status))
 	goto BAIL;
 
@@ -2218,7 +2265,7 @@
     }
 
     status = _cairo_pdf_surface_emit_image (surface, (cairo_image_surface_t *)pad_image,
-                                            resource, interpolate);
+                                            resource, interpolate, FALSE);
     if (unlikely (status))
         goto BAIL;
 
@@ -2377,7 +2424,8 @@
 	return _cairo_pdf_surface_emit_image_surface (surface,
 						      src_surface->surface,
 						      src_surface->hash_entry->surface_res,
-						      src_surface->hash_entry->interpolate);
+						      src_surface->hash_entry->interpolate,
+						      src_surface->hash_entry->mask);
     }
 }
 
@@ -2414,6 +2462,7 @@
 	status = _cairo_pdf_surface_add_source_surface (surface,
 							pattern->surface,
 							pdf_pattern->pattern->filter,
+							FALSE,
 							&pattern_resource,
 							&pattern_width,
 							&pattern_height);
@@ -3375,7 +3424,8 @@
 
 static cairo_status_t
 _cairo_pdf_surface_paint_surface_pattern (cairo_pdf_surface_t     *surface,
-					  cairo_surface_pattern_t *source)
+					  cairo_surface_pattern_t *source,
+					  cairo_bool_t             mask)
 {
     cairo_pdf_resource_t surface_res;
     int width, height;
@@ -3386,6 +3436,7 @@
     status = _cairo_pdf_surface_add_source_surface (surface,
 						    source->surface,
 						    source->base.filter,
+						    mask,
 						    &surface_res,
 						    &width,
 						    &height);
@@ -3420,10 +3471,16 @@
     if (unlikely (status))
 	return status;
 
-    _cairo_output_stream_printf (surface->output,
-				 "/a%d gs /x%d Do\n",
-				 alpha,
-				 surface_res.id);
+    if (mask) {
+	_cairo_output_stream_printf (surface->output,
+				     "/x%d Do\n",
+				     surface_res.id);
+    } else {
+	_cairo_output_stream_printf (surface->output,
+				     "/a%d gs /x%d Do\n",
+				     alpha,
+				     surface_res.id);
+    }
 
     return _cairo_pdf_surface_add_xobject (surface, surface_res);
 }
@@ -5408,6 +5465,68 @@
     return _cairo_pdf_surface_open_content_stream (surface, NULL, TRUE);
 }
 
+/* A PDF stencil mask is an A1 mask used with the current color */
+static cairo_int_status_t
+_cairo_pdf_surface_emit_stencil_mask (cairo_pdf_surface_t   *surface,
+				      const cairo_pattern_t *source,
+				      const cairo_pattern_t *mask)
+{
+    cairo_status_t status;
+    cairo_surface_pattern_t *surface_pattern;
+    cairo_image_surface_t  *image;
+    void		   *image_extra;
+    cairo_pdf_resource_t pattern_res = {0};
+
+    if (! (source->type == CAIRO_PATTERN_TYPE_SOLID &&
+	   mask->type == CAIRO_PATTERN_TYPE_SURFACE))
+	return CAIRO_INT_STATUS_UNSUPPORTED;
+
+    surface_pattern = (cairo_surface_pattern_t *) mask;
+    if (surface_pattern->surface->type == CAIRO_SURFACE_TYPE_RECORDING)
+	return CAIRO_INT_STATUS_UNSUPPORTED;
+
+    status = _cairo_surface_acquire_source_image (surface_pattern->surface,
+						  &image,
+						  &image_extra);
+    if (unlikely (status))
+	return status;
+
+    if (image->base.status)
+	return image->base.status;
+
+    if (image->format != CAIRO_FORMAT_A1) {
+	status = CAIRO_INT_STATUS_UNSUPPORTED;
+	goto cleanup;
+    }
+
+    status = _cairo_pdf_surface_select_pattern (surface, source,
+						pattern_res, FALSE);
+    if (unlikely (status))
+	return status;
+
+    status = _cairo_pdf_operators_flush (&surface->pdf_operators);
+    if (unlikely (status))
+	return status;
+
+    _cairo_output_stream_printf (surface->output, "q\n");
+    status = _cairo_pdf_surface_paint_surface_pattern (surface,
+						       (cairo_surface_pattern_t *) surface_pattern,
+						       TRUE);
+    if (unlikely (status))
+	return status;
+
+    _cairo_output_stream_printf (surface->output, "Q\n");
+
+    _cairo_surface_release_source_image (surface_pattern->surface, image, image_extra);
+    status = _cairo_output_stream_get_status (surface->output);
+
+cleanup:
+    _cairo_surface_release_source_image (surface_pattern->surface, image, image_extra);
+
+    return status;
+}
+
+
 static cairo_int_status_t
 _cairo_pdf_surface_paint (void			*abstract_surface,
 			  cairo_operator_t	 op,
@@ -5457,7 +5576,8 @@
     {
 	_cairo_output_stream_printf (surface->output, "q\n");
 	status = _cairo_pdf_surface_paint_surface_pattern (surface,
-							   (cairo_surface_pattern_t *) source);
+							   (cairo_surface_pattern_t *) source,
+							   FALSE);
 	if (unlikely (status))
 	    return status;
 
@@ -5524,7 +5644,7 @@
 }
 
 static cairo_int_status_t
-_cairo_pdf_surface_mask	(void			*abstract_surface,
+_cairo_pdf_surface_mask (void			*abstract_surface,
 			 cairo_operator_t	 op,
 			 const cairo_pattern_t	*source,
 			 const cairo_pattern_t	*mask,
@@ -5575,6 +5695,15 @@
     if (unlikely (status))
 	return status;
 
+    status = _cairo_pdf_surface_select_operator (surface, op);
+    if (unlikely (status))
+	return status;
+
+    /* Check if we can use a stencil mask */
+    status = _cairo_pdf_surface_emit_stencil_mask (surface, source, mask);
+    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
+	return status;
+
     group = _cairo_pdf_surface_create_smask_group (surface);
     if (unlikely (group == NULL))
 	return _cairo_error (CAIRO_STATUS_NO_MEMORY);
@@ -5614,10 +5743,6 @@
     if (unlikely (status))
 	return status;
 
-    status = _cairo_pdf_surface_select_operator (surface, op);
-    if (unlikely (status))
-	return status;
-
     _cairo_output_stream_printf (surface->output,
 				 "q /s%d gs /x%d Do Q\n",
 				 group->group_res.id,
@@ -5829,7 +5954,8 @@
 	    return status;
 
 	status = _cairo_pdf_surface_paint_surface_pattern (surface,
-							   (cairo_surface_pattern_t *) source);
+							   (cairo_surface_pattern_t *) source,
+							   FALSE);
 	if (unlikely (status))
 	    return status;
 
