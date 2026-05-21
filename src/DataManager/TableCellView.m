// azul
// Copyright © 2016-2026 Ken Arroyo Ohori
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#import <TargetConditionals.h>

#if TARGET_OS_OSX

#import "TableCellView.h"

@implementation TableCellView

@synthesize checkBox;

- (TableCellView *)initWithFrame:(NSRect)frameRect {
//  NSLog(@"[TableCellView initWithFrame]");
  if (self = [super initWithFrame:frameRect]) {
    
    checkBox = [[NSButton alloc] initWithFrame:NSZeroRect];
    checkBox.translatesAutoresizingMaskIntoConstraints = NO;
    [checkBox setButtonType:NSButtonTypeSwitch];
    [checkBox setAllowsMixedState:YES];
    
    image = [[NSImageView alloc] initWithFrame:NSZeroRect];
    image.translatesAutoresizingMaskIntoConstraints = NO;
    [image setImageScaling:NSImageScaleProportionallyUpOrDown];
    [image setImageAlignment:NSImageAlignCenter];
    
    text = [[NSTextField alloc] initWithFrame:NSZeroRect];
    text.translatesAutoresizingMaskIntoConstraints = NO;
    [text setDrawsBackground:false];
    [text setBordered:false];
    [text setEditable:false];
    [text setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
    text.usesSingleLineMode = YES;
    text.lineBreakMode = NSLineBreakByTruncatingTail;
    
    self.identifier = @"TableCellView";
    [self setImageView:image];
    [self setTextField:text];
    [self addSubview:image];
    [self addSubview:checkBox];
    [self addSubview:text];
    
    [NSLayoutConstraint activateConstraints:@[
      // Checkbox: leading, centered vertically, fixed size
      [checkBox.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16],
      [checkBox.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
      [checkBox.widthAnchor constraintEqualToConstant:14],
      [checkBox.heightAnchor constraintEqualToConstant:14],
      
      // Image: trailing checkbox, centered vertically, fixed size
      [image.leadingAnchor constraintEqualToAnchor:checkBox.trailingAnchor constant:3],
      [image.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
      [image.widthAnchor constraintEqualToConstant:16],
      [image.heightAnchor constraintEqualToConstant:16],
      
      // Text: trailing image, fills remaining width, centered vertically
      [text.leadingAnchor constraintEqualToAnchor:image.trailingAnchor constant:3],
      [text.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-4],
      [text.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    ]];
    
  } return self;
}

- (void)prepareForReuse {
  [super prepareForReuse];
  [checkBox setState:NSControlStateValueOff];
  [checkBox setAction:nil];
  [checkBox setTarget:nil];
  [[self imageView] setImage:nil];
  [[self textField] setStringValue:@""];
}

@end

#endif
